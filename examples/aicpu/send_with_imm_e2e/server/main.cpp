/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "send_with_imm_e2e.h"

#include "acl/acl_base.h"
#include "acl/acl_rt.h"
#include "hcomm_channel.h"
#include "hcomm_res.h"
#include "hcomm_res_defs.h"
#include "hccl/hccl_types.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace send_with_imm_e2e;

bool ReadExact(int fd, void* data, uint32_t bytes)
{
    auto* cursor = static_cast<uint8_t*>(data);
    uint32_t done = 0;
    while (done < bytes) {
        ssize_t n = ::recv(fd, cursor + done, bytes - done, 0);
        if (n <= 0) return false;
        done += static_cast<uint32_t>(n);
    }
    return true;
}

bool WriteExact(int fd, const void* data, uint32_t bytes)
{
    const auto* cursor = static_cast<const uint8_t*>(data);
    uint32_t done = 0;
    while (done < bytes) {
        ssize_t n = ::send(fd, cursor + done, bytes - done, 0);
        if (n <= 0) return false;
        done += static_cast<uint32_t>(n);
    }
    return true;
}

bool ReadMessage(int fd, MessageHeader* header, std::vector<uint8_t>* body)
{
    if (!ReadExact(fd, header, sizeof(*header))) return false;
    body->resize(header->bytes);
    return header->bytes == 0 || ReadExact(fd, body->data(), header->bytes);
}

bool WriteMessage(int fd, MessageKind kind, const void* body, uint32_t bytes)
{
    MessageHeader header{static_cast<uint32_t>(kind), bytes};
    return WriteExact(fd, &header, sizeof(header)) && (bytes == 0 || WriteExact(fd, body, bytes));
}

bool QueryHostEndpoint(EndpointDesc* endpoint)
{
    uint32_t count = 0;
    HcommResult ret = HcommEndpointGetDescNum(0, &count);
    if (ret != HCCL_SUCCESS) return false;
    if (count == 0) return false;

    std::vector<EndpointDesc> endpoints(count);
    uint32_t inOutCount = count;
    ret = HcommEndpointGetDescs(0, &inOutCount, endpoints.data());
    if (ret != HCCL_SUCCESS) return false;
    for (uint32_t i = 0; i < inOutCount; ++i) {
        if (endpoints[i].loc.locType == ENDPOINT_LOC_TYPE_HOST &&
            (endpoints[i].protocol == COMM_PROTOCOL_UB_CTP || endpoints[i].protocol == COMM_PROTOCOL_UBC_TP)) {
            *endpoint = endpoints[i];
            return true;
        }
    }
    *endpoint = endpoints[0];
    return true;
}

void PrintUsage()
{
    std::fprintf(stderr, "Usage: send_with_imm_server [--port P] [--len N] [--imm V] [--count N] [--recv-slots N]\n");
}

int RunServer(const TestConfig& config)
{
    if (aclInit(nullptr) != ACL_SUCCESS) return 1;
    if (aclrtSetDevice(0) != ACL_SUCCESS) {
        (void)aclFinalize();
        return 1;
    }


    EndpointDesc localEndpoint{};
    EndpointHandle endpoint = nullptr;
    HcommMemHandle recvMemHandle = nullptr;
    int clientFd = -1;
    ChannelHandle channel = 0;
    void* recvBuffer = nullptr;
    int exitCode = 1;
    do {
        if (!QueryHostEndpoint(&localEndpoint)) break;
        if (HcommEndpointCreate(&localEndpoint, &endpoint) != HCCL_SUCCESS) break;

        int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) break;
        int reuse = 1;
        (void)::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(config.listenPort);
        if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listenFd, 1) != 0) {
            ::close(listenFd);
            break;
        }
        std::fprintf(stderr, "[send-imm-server] listening on port %u\n", config.listenPort);
        clientFd = ::accept(listenFd, nullptr, nullptr);
        ::close(listenFd);
        if (clientFd < 0) break;

        MessageHeader header{};
        std::vector<uint8_t> body;
        EndpointDesc remoteEndpoint{};
        if (!ReadMessage(clientFd, &header, &body) || header.kind != kEndpointDesc ||
            body.size() != sizeof(remoteEndpoint)) {
            break;
        }
        std::memcpy(&remoteEndpoint, body.data(), sizeof(remoteEndpoint));

        HcommChannelDesc desc{};
        if (HcommChannelDescInit(&desc, 1) != HCCL_SUCCESS) break;
        desc.remoteEndpoint = remoteEndpoint;
        desc.role = HCOMM_SOCKET_ROLE_SERVER;
        desc.port = 60001;
        desc.exchangeAllMems = true;
        if (HcommChannelCreate(endpoint, COMM_ENGINE_CPU, &desc, 1, &channel) != HCCL_SUCCESS) break;

        int32_t status = 0;
        HcommResult ret;
        do {
            ret = HcommChannelGetStatus(&channel, 1, &status);
            if (ret != HCCL_SUCCESS) break;
        } while (status == HCOMM_CHANNEL_STATUS_CONNECTING);
        if (ret != HCCL_SUCCESS || status != HCOMM_CHANNEL_STATUS_READY) break;

        const uint32_t recvSlots = std::min(config.recvSlots, config.sendCount);
        if (recvSlots == 0) {
            std::fprintf(stderr, "[send-imm-server] recvSlots is 0.\n");
            break;
        }
        const size_t bufferBytes = static_cast<size_t>(config.payloadBytes) * recvSlots;
        recvBuffer = std::malloc(bufferBytes == 0 ? 1 : bufferBytes);
        if (recvBuffer == nullptr) break;
        std::memset(recvBuffer, 0, bufferBytes);

        CommMem recvMem{};
        recvMem.type = COMM_MEM_TYPE_HOST;
        recvMem.addr = recvBuffer;
        recvMem.size = bufferBytes;
        if (HcommMemReg(endpoint, "send_with_imm_recv", &recvMem, &recvMemHandle) != HCCL_SUCCESS) break;

        bool posted = true;
        for (uint32_t i = 0; i < recvSlots; ++i) {
            auto* dst = static_cast<uint8_t*>(recvBuffer) + static_cast<size_t>(i) * config.payloadBytes;
            if (HcommChannelPostRecv(channel, dst, config.payloadBytes, i) != HCCL_SUCCESS) {
                posted = false;
                break;
            }
        }
        if (!posted) break;

        void* memDesc = nullptr;
        uint32_t memDescLen = 0;
        if (HcommMemExport(endpoint, recvMemHandle, &memDesc, &memDescLen) != HCCL_SUCCESS) break;
        if (!WriteMessage(clientFd, kMemoryDesc, memDesc, memDescLen)) break;

        MessageHeader readyHeader{};
        std::vector<uint8_t> readyBody;
        if (!ReadMessage(clientFd, &readyHeader, &readyBody) || readyHeader.kind != kClientReady ||
            readyHeader.bytes != 0) {
            break;
        }
        if (!WriteMessage(clientFd, kReady, nullptr, 0)) break;

        std::vector<HcommRecvCompletion> completions(recvSlots);
        uint32_t received = 0;
        uint32_t errors = 0;
        while (received < config.sendCount) {
            uint32_t count = 0;
            ret = HcommChannelPollJfc(channel, completions.data(), recvSlots, &count);
            if (ret != HCCL_SUCCESS) {
                ++errors;
                break;
            }
            for (uint32_t i = 0; i < count; ++i) {
                const auto& completion = completions[i];
                bool valid = completion.ok && completion.isRecv && completion.hasImmData &&
                    completion.wrId < recvSlots && completion.immData == config.immData &&
                    completion.byteLen == config.payloadBytes;
                if (valid) {
                    const auto* payload = static_cast<const uint8_t*>(recvBuffer) +
                        static_cast<size_t>(completion.wrId) * config.payloadBytes;
                    for (uint32_t byteIndex = 0; byteIndex < config.payloadBytes; ++byteIndex) {
                        if (payload[byteIndex] != static_cast<uint8_t>(byteIndex)) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (!valid) ++errors;
                ++received;
                if (received < config.sendCount) {
                    auto* dst = static_cast<uint8_t*>(recvBuffer) +
                        static_cast<size_t>(completion.wrId) * config.payloadBytes;
                    (void)HcommChannelPostRecv(channel, dst, config.payloadBytes, completion.wrId);
                }
            }
        }

        TestResult result{received, errors, 0};
        (void)WriteMessage(clientFd, kResult, &result, sizeof(result));
        std::fprintf(stderr, "[send-imm-server] received=%u errors=%u expected=%u\n", received, errors,
            config.sendCount);
        exitCode = (received == config.sendCount && errors == 0) ? 0 : 1;
    } while (false);

    std::free(recvBuffer);
    if (recvMemHandle != nullptr) (void)HcommMemUnreg(endpoint, recvMemHandle);
    if (channel != 0) (void)HcommChannelDestroy(&channel, 1);
    if (clientFd >= 0) ::close(clientFd);
    if (endpoint != nullptr) (void)HcommEndpointDestroy(endpoint);
    (void)aclrtResetDevice(0);
    (void)aclFinalize();
    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    TestConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
        if (arg == "--port") config.listenPort = static_cast<uint16_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--len") config.payloadBytes = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--imm") config.immData = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--count") config.sendCount = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--recv-slots") config.recvSlots = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else {
            PrintUsage();
            return 2;
        }
    }
    if (config.payloadBytes == 0 || config.sendCount == 0 || config.recvSlots == 0) {
        PrintUsage();
        return 2;
    }
    return RunServer(config);
}
