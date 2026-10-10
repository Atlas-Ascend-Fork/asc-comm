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
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace send_with_imm_e2e;

bool CheckAcl(aclError ret, const char* stage)
{
    if (ret == ACL_SUCCESS) return true;
    std::fprintf(stderr, "[send-imm-server] %s failed, ret=%d\n", stage, static_cast<int>(ret));
    return false;
}

bool CheckHcomm(HcommResult ret, const char* stage)
{
    if (ret == HCCL_SUCCESS) return true;
    std::fprintf(stderr, "[send-imm-server] %s failed, ret=%d\n", stage, static_cast<int>(ret));
    return false;
}

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

bool WaitChannelReady(ChannelHandle channel, uint32_t timeoutMs)
{
    int32_t status = 0;
    HcommResult ret;
    uint32_t elapsedMs = 0;
    constexpr uint32_t pollIntervalMs = 10;
    do {
        ret = HcommChannelGetStatus(&channel, 1, &status);
        if (ret != HCCL_SUCCESS) {
            std::fprintf(stderr, "[send-imm-server] get channel status failed: %d\n", static_cast<int>(ret));
            return false;
        }
        if (status != HCOMM_CHANNEL_STATUS_CONNECTING) break;
        if (elapsedMs >= timeoutMs) {
            std::fprintf(
                stderr, "[send-imm-server] channel connect timeout after %u ms, status=%d\n", timeoutMs, status);
            return false;
        }
        usleep(pollIntervalMs * 1000);
        elapsedMs += pollIntervalMs;
    } while (true);
    if (status != HCOMM_CHANNEL_STATUS_READY) {
        std::fprintf(stderr, "[send-imm-server] channel is not ready, status=%d\n", status);
        return false;
    }
    return true;
}

bool ReuseDeviceEndpoint(int32_t deviceId, EndpointDesc* endpoint)
{
    uint32_t count = 0;
    HcommResult ret = HcommEndpointGetDescNum(deviceId, &count);
    if (!CheckHcomm(ret, "query endpoint count")) return false;
    if (count == 0) {
        std::fprintf(stderr, "[send-imm-server] no endpoint was found, device=%d\n", deviceId);
        return false;
    }
    std::vector<EndpointDesc> endpoints(count);
    uint32_t inOutCount = count;
    ret = HcommEndpointGetDescs(deviceId, &inOutCount, endpoints.data());
    if (!CheckHcomm(ret, "query endpoint descriptors")) return false;
    for (uint32_t i = 0; i < inOutCount; ++i) {
        if (endpoints[i].protocol == COMM_PROTOCOL_UB_CTP || endpoints[i].protocol == COMM_PROTOCOL_UBC_TP) {
            *endpoint = endpoints[i];
            std::fprintf(stderr, "[send-imm-server] reuse device endpoint for CPU channel, index=%u, protocol=%d\n",
                i, static_cast<int>(endpoint->protocol));
            return true;
        }
    }
    std::fprintf(stderr, "[send-imm-server] no UB endpoint was found, endpoints=%u\n", inOutCount);
    return false;
}

bool BuildHostEndpoint(const std::string& hostIp, EndpointDesc* endpoint)
{
    if (hostIp.empty()) return false;
    *endpoint = EndpointDesc{};
    endpoint->protocol = COMM_PROTOCOL_UB_CTP;
    endpoint->commAddr.type = COMM_ADDR_TYPE_IP_V4;
    endpoint->loc.locType = ENDPOINT_LOC_TYPE_HOST;
    if (::inet_pton(AF_INET, hostIp.c_str(), &endpoint->commAddr.addr) != 1) {
        std::fprintf(stderr, "[send-imm-server] host IP is invalid: %s\n", hostIp.c_str());
        return false;
    }
    return true;
}

void PrintUsage()
{
    std::fprintf(
        stderr, "Usage: send_with_imm_server [--port P] [--host-ip IP] [--len N] [--imm V] [--count N] [--recv-slots N]\n");
}

int RunServer(TestConfig config)
{
    if (!CheckAcl(aclInit(nullptr), "initialize ACL runtime")) return 1;
    if (!CheckAcl(aclrtSetDevice(0), "set device")) {
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
    int listenFd = -1;
    do {
        listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) {
            std::fprintf(stderr, "[send-imm-server] create TCP socket failed, errno=%d\n", errno);
            break;
        }
        int reuse = 1;
        (void)::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(config.listenPort);
        if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::fprintf(stderr, "[send-imm-server] bind TCP port failed, port=%u, errno=%d\n",
                config.listenPort, errno);
            ::close(listenFd);
            break;
        }
        if (::listen(listenFd, 1) != 0) {
            std::fprintf(stderr, "[send-imm-server] listen TCP port failed, port=%u, errno=%d\n",
                config.listenPort, errno);
            ::close(listenFd);
            break;
        }
        std::fprintf(stderr, "[send-imm-server] listening on port %u\n", config.listenPort);

        if (!config.hostIp.empty()) {
            std::fprintf(stderr, "[send-imm-server] build host endpoint, ip=%s\n", config.hostIp.c_str());
            if (!BuildHostEndpoint(config.hostIp, &localEndpoint)) break;
        } else {
            std::fprintf(stderr, "[send-imm-server] reuse device endpoint from endpoint query\n");
            if (!ReuseDeviceEndpoint(0, &localEndpoint)) break;
        }
        std::fprintf(stderr, "[send-imm-server] create host endpoint\n");
        if (!CheckHcomm(HcommEndpointCreate(&localEndpoint, &endpoint), "create host endpoint")) break;

        clientFd = ::accept(listenFd, nullptr, nullptr);
        ::close(listenFd);
        listenFd = -1;
        if (clientFd < 0) {
            std::fprintf(stderr, "[send-imm-server] accept client failed, errno=%d\n", errno);
            break;
        }
        std::fprintf(stderr, "[send-imm-server] client connected\n");

        MessageHeader header{};
        std::vector<uint8_t> body;
        EndpointDesc remoteEndpoint{};
        if (!ReadMessage(clientFd, &header, &body) || header.kind != kEndpointDesc ||
            body.size() != sizeof(remoteEndpoint)) {
            std::fprintf(stderr, "[send-imm-server] receive client endpoint failed\n");
            break;
        }
        std::memcpy(&remoteEndpoint, body.data(), sizeof(remoteEndpoint));
        std::fprintf(stderr, "[send-imm-server] received client endpoint\n");

        if (!WriteMessage(clientFd, kEndpointDesc, &localEndpoint, sizeof(localEndpoint))) {
            std::fprintf(stderr, "[send-imm-server] send server endpoint failed\n");
            break;
        }

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
        if (!CheckHcomm(HcommMemReg(endpoint, "send_with_imm_recv", &recvMem, &recvMemHandle),
            "register receive memory")) {
            break;
        }
        std::fprintf(stderr, "[send-imm-server] receive memory registered\n");

        void* memDesc = nullptr;
        uint32_t memDescLen = 0;
        if (!CheckHcomm(HcommMemExport(endpoint, recvMemHandle, &memDesc, &memDescLen), "export receive memory")) {
            break;
        }
        if (!WriteMessage(clientFd, kMemoryDesc, memDesc, memDescLen)) {
            std::fprintf(stderr, "[send-imm-server] send memory descriptor failed\n");
            break;
        }

        HcommChannelDesc desc{};
        if (!CheckHcomm(HcommChannelDescInit(&desc, 1), "init channel descriptor")) break;
        desc.remoteEndpoint = remoteEndpoint;
        desc.role = HCOMM_SOCKET_ROLE_SERVER;
        desc.port = 60001;
        desc.exchangeAllMems = true;
        std::fprintf(stderr, "[send-imm-server] create CPU channel, port=60001\n");
        if (!CheckHcomm(HcommChannelCreate(endpoint, COMM_ENGINE_CPU, &desc, 1, &channel), "create CPU channel")) {
            break;
        }
        if (!WaitChannelReady(channel, kDefaultConnectTimeoutMs)) break;
        std::fprintf(stderr, "[send-imm-server] CPU channel ready\n");

        bool posted = true;
        for (uint32_t i = 0; i < recvSlots; ++i) {
            auto* dst = static_cast<uint8_t*>(recvBuffer) + static_cast<size_t>(i) * config.payloadBytes;
            if (!CheckHcomm(HcommChannelPostRecv(channel, dst, config.payloadBytes, i), "post receive buffer")) {
                posted = false;
                break;
            }
        }
        if (!posted) break;
        std::fprintf(stderr, "[send-imm-server] posted receive slots=%u\n", recvSlots);

        MessageHeader readyHeader{};
        std::vector<uint8_t> readyBody;
        if (!ReadMessage(clientFd, &readyHeader, &readyBody) || readyHeader.kind != kClientReady ||
            readyHeader.bytes != 0) {
            std::fprintf(stderr, "[send-imm-server] receive client-ready message failed\n");
            break;
        }
        if (!WriteMessage(clientFd, kReady, nullptr, 0)) {
            std::fprintf(stderr, "[send-imm-server] send server-ready message failed\n");
            break;
        }
        std::fprintf(stderr, "[send-imm-server] wait for AICPU send completion\n");

        std::vector<HcommRecvCompletion> completions(recvSlots);
        uint32_t received = 0;
        uint32_t errors = 0;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(kDefaultRecvTimeoutMs);
        bool recvTimeout = false;
        HcommResult ret = HCCL_SUCCESS;
        while (received < config.sendCount) {
            uint32_t count = 0;
            ret = HcommChannelPollJfc(channel, completions.data(), recvSlots, &count);
            if (!CheckHcomm(ret, "poll receive completion")) {
                ++errors;
                break;
            }
            if (count == 0) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    recvTimeout = true;
                    ++errors;
                    std::fprintf(
                        stderr, "[send-imm-server] wait completion timeout after %u ms, received=%u\n",
                        kDefaultRecvTimeoutMs, received);
                    break;
                }
                usleep(1000);
                continue;
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
                if (!valid) {
                    std::fprintf(stderr,
                        "[send-imm-server] invalid completion: wrId=%llu immData=%llu byteLen=%u ok=%d "
                        "isRecv=%d hasImmData=%d\n",
                        static_cast<unsigned long long>(completion.wrId),
                        static_cast<unsigned long long>(completion.immData), completion.byteLen,
                        static_cast<int>(completion.ok), static_cast<int>(completion.isRecv),
                        static_cast<int>(completion.hasImmData));
                }
                ++received;
                if (received < config.sendCount) {
                    auto* dst = static_cast<uint8_t*>(recvBuffer) +
                        static_cast<size_t>(completion.wrId) * config.payloadBytes;
                    (void)HcommChannelPostRecv(channel, dst, config.payloadBytes, completion.wrId);
                }
            }
        }

        TestResult result{received, errors, recvTimeout ? -1 : 0};
        (void)WriteMessage(clientFd, kResult, &result, sizeof(result));
        std::fprintf(stderr, "[send-imm-server] received=%u errors=%u expected=%u\n", received, errors,
            config.sendCount);
        exitCode = (received == config.sendCount && errors == 0) ? 0 : 1;
    } while (false);

    std::free(recvBuffer);
    if (recvMemHandle != nullptr) (void)HcommMemUnreg(endpoint, recvMemHandle);
    if (channel != 0) (void)HcommChannelDestroy(&channel, 1);
    if (clientFd >= 0) ::close(clientFd);
    if (listenFd >= 0) ::close(listenFd);
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
        else if (arg == "--host-ip") config.hostIp = next();
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
