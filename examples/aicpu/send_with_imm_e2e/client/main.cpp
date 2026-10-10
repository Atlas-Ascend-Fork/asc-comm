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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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

bool QueryDeviceEndpoint(int32_t deviceId, EndpointDesc* endpoint)
{
    uint32_t count = 0;
    HcommResult ret = HcommEndpointGetDescNum(deviceId, &count);
    if (ret != HCCL_SUCCESS) return false;
    if (count == 0) return false;
    std::vector<EndpointDesc> endpoints(count);
    uint32_t inOutCount = count;
    ret = HcommEndpointGetDescs(deviceId, &inOutCount, endpoints.data());
    if (ret != HCCL_SUCCESS) return false;
    for (uint32_t i = 0; i < inOutCount; ++i) {
        if ((endpoints[i].protocol == COMM_PROTOCOL_UB_CTP || endpoints[i].protocol == COMM_PROTOCOL_UBC_TP) &&
            endpoints[i].loc.locType == ENDPOINT_LOC_TYPE_DEVICE) {
            *endpoint = endpoints[i];
            return true;
        }
    }
    *endpoint = endpoints[0];
    return true;
}

void PrintUsage()
{
    std::fprintf(stderr, "Usage: send_with_imm_client --host IP [--port P] [--device N] [--len N] [--imm V] [--count N]\n");
}

} // namespace

struct SendWithImmKernelArgs {
    ThreadHandle thread{0};
    ChannelHandle channel{0};
    uintptr_t src{0};
    uint64_t len{0};
    uint32_t immData{0};
    int32_t ret{-1};
};

static bool LoadAndLaunchSendKernel(
    const SendWithImmKernelArgs& args, const char* jsonPath, aclrtStream stream, int32_t* kernelRet)
{
    if (jsonPath == nullptr || stream == nullptr || kernelRet == nullptr) return false;

    aclrtBinaryLoadOptions options{};
    aclrtBinaryLoadOption option{};
    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 1;
    options.numOpt = 1;
    options.options = &option;

    aclrtBinHandle binHandle = nullptr;
    if (aclrtBinaryLoadFromFile(jsonPath, &options, &binHandle) != ACL_SUCCESS || binHandle == nullptr) {
        std::fprintf(stderr, "[send-imm-client] load AICPU json failed: %s\n", jsonPath);
        return false;
    }

    aclrtFuncHandle funcHandle = nullptr;
    bool ok = aclrtBinaryGetFunction(binHandle, "AsccommSendWithImmKernel", &funcHandle) == ACL_SUCCESS &&
        funcHandle != nullptr;
    if (!ok) {
        std::fprintf(stderr, "[send-imm-client] get kernel function failed: %s\n", jsonPath);
        (void)aclrtBinaryUnLoad(binHandle);
        return false;
    }

    aclrtLaunchKernelCfg cfg{};
    ok = aclrtLaunchKernelWithHostArgs(
             funcHandle, 1, stream, &cfg, const_cast<void*>(static_cast<const void*>(&args)), sizeof(args), nullptr, 0) ==
        ACL_SUCCESS;
    if (!ok) {
        std::fprintf(stderr, "[send-imm-client] launch AICPU kernel failed\n");
        (void)aclrtBinaryUnLoad(binHandle);
        return false;
    }

    ok = aclrtSynchronizeStream(stream) == ACL_SUCCESS;
    if (!ok) {
        std::fprintf(stderr, "[send-imm-client] synchronize AICPU stream failed\n");
    }
    *kernelRet = ok ? 0 : std::numeric_limits<int32_t>::min();
    (void)aclrtBinaryUnLoad(binHandle);
    return ok;
}

int main(int argc, char** argv)
{
    TestConfig config;
    std::string host = "127.0.0.1";
    int32_t deviceId = 0;
    std::string kernelJson;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? argv[++i] : ""; };
        if (arg == "--host") host = next();
        else if (arg == "--port") config.listenPort = static_cast<uint16_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--device") deviceId = static_cast<int32_t>(std::strtol(next(), nullptr, 0));
        else if (arg == "--len") config.payloadBytes = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--imm") config.immData = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--count") config.sendCount = static_cast<uint32_t>(std::strtoul(next(), nullptr, 0));
        else if (arg == "--kernel-json") kernelJson = next();
        else {
            PrintUsage();
            return 2;
        }
    }
    if (host.empty() || config.payloadBytes == 0 || config.sendCount == 0 || kernelJson.empty()) {
        PrintUsage();
        return 2;
    }

    if (aclInit(nullptr) != ACL_SUCCESS) return 1;
    if (aclrtSetDevice(deviceId) != ACL_SUCCESS) {
        (void)aclFinalize();
        return 1;
    }

    int exitCode = 1;
    int fd = -1;
    EndpointHandle endpoint = nullptr;
    ChannelHandle channel = 0;
    ThreadHandle thread = 0;
    aclrtStream stream = nullptr;
    HcommMemHandle sendMemHandle = nullptr;
    bool recvMemImported = false;
    std::vector<uint8_t> recvMemDesc;
    void* sendBuffer = nullptr;
    do {
        EndpointDesc localEndpoint{};
        if (!QueryDeviceEndpoint(deviceId, &localEndpoint)) break;
        if (HcommEndpointCreate(&localEndpoint, &endpoint) != HCCL_SUCCESS) break;

        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) break;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.listenPort);
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
            ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            break;
        }
        if (!WriteMessage(fd, kEndpointDesc, &localEndpoint, sizeof(localEndpoint))) break;

        MessageHeader header{};
        std::vector<uint8_t> body;
        EndpointDesc remoteEndpoint{};
        if (!ReadMessage(fd, &header, &body) || header.kind != kEndpointDesc ||
            body.size() != sizeof(remoteEndpoint)) {
            break;
        }
        std::memcpy(&remoteEndpoint, body.data(), sizeof(remoteEndpoint));

        if (!ReadMessage(fd, &header, &body) || header.kind != kMemoryDesc || body.empty()) break;
        CommMem importedRecvMem{};
        if (HcommMemImport(endpoint, body.data(), static_cast<uint32_t>(body.size()), &importedRecvMem) !=
            HCCL_SUCCESS) {
            break;
        }
        recvMemImported = true;
        recvMemDesc = body;

        HcommChannelDesc desc{};
        if (HcommChannelDescInit(&desc, 1) != HCCL_SUCCESS) break;
        desc.remoteEndpoint = remoteEndpoint;
        desc.role = HCOMM_SOCKET_ROLE_CLIENT;
        desc.port = 60001;
        desc.exchangeAllMems = true;
        if (HcommChannelCreate(endpoint, COMM_ENGINE_AICPU_TS, &desc, 1, &channel) != HCCL_SUCCESS) break;

        int32_t status = 0;
        HcommResult ret;
        do {
            ret = HcommChannelGetStatus(&channel, 1, &status);
            if (ret != HCCL_SUCCESS) break;
        } while (status == HCOMM_CHANNEL_STATUS_CONNECTING);
        if (ret != HCCL_SUCCESS || status != HCOMM_CHANNEL_STATUS_READY) break;

        ThreadConfig threadConfig{};
        if (ThreadConfigInit(&threadConfig, 1) != HCCL_SUCCESS) break;
        const uint32_t notifyNumPerThread = 0;
        if (HcommThreadAlloc(COMM_ENGINE_AICPU_TS, 1, &notifyNumPerThread, &thread) != HCCL_SUCCESS) break;

        if (aclrtCreateStream(&stream) != ACL_SUCCESS || stream == nullptr) break;
        if (aclrtMalloc(&sendBuffer, config.payloadBytes, ACL_MEM_MALLOC_HUGE_ONLY) != ACL_SUCCESS ||
            sendBuffer == nullptr) {
            break;
        }
        std::vector<uint8_t> pattern(config.payloadBytes);
        for (uint32_t i = 0; i < config.payloadBytes; ++i) pattern[i] = static_cast<uint8_t>(i);
        if (aclrtMemcpy(sendBuffer, config.payloadBytes, pattern.data(), config.payloadBytes, ACL_MEMCPY_HOST_TO_DEVICE) !=
            ACL_SUCCESS) {
            break;
        }

        CommMem sendMem{};
        sendMem.type = COMM_MEM_TYPE_DEVICE;
        sendMem.addr = sendBuffer;
        sendMem.size = config.payloadBytes;
        if (HcommMemReg(endpoint, "send_with_imm_send", &sendMem, &sendMemHandle) != HCCL_SUCCESS) break;

        ChannelHandle deviceChannel = 0;
        if (HcommChannelBuildDeviceEntity(channel, &deviceChannel) != HCCL_SUCCESS || deviceChannel == 0) break;

        if (!WriteMessage(fd, kClientReady, nullptr, 0)) break;
        if (!ReadMessage(fd, &header, &body) || header.kind != kReady || header.bytes != 0) break;

        SendWithImmKernelArgs args{};
        args.thread = thread;
        args.channel = deviceChannel;
        args.src = reinterpret_cast<uintptr_t>(sendBuffer);
        args.len = config.payloadBytes;
        args.immData = config.immData;
        bool sendOk = true;
        for (uint32_t i = 0; i < config.sendCount; ++i) {
            int32_t kernelRet = 0;
            if (!LoadAndLaunchSendKernel(args, kernelJson.c_str(), stream, &kernelRet)) {
                sendOk = false;
                break;
            }
            if (kernelRet != 0) {
                std::fprintf(stderr, "[send-imm-client] AICPU send %u returned %d\n", i, kernelRet);
                sendOk = false;
                break;
            }
        }
        if (!sendOk) break;

        TestResult result{};
        if (!ReadMessage(fd, &header, &body) || header.kind != kResult || body.size() != sizeof(result)) break;
        std::memcpy(&result, body.data(), sizeof(result));
        exitCode = (result.received == config.sendCount && result.errors == 0 && result.apiReturn == 0) ? 0 : 1;
        std::fprintf(stderr, "[send-imm-client] received=%u errors=%u apiReturn=%d expected=%u\n", result.received,
            result.errors, result.apiReturn, config.sendCount);
    } while (false);

    if (sendMemHandle != nullptr) (void)HcommMemUnreg(endpoint, sendMemHandle);
    if (recvMemImported && !recvMemDesc.empty()) {
        (void)HcommMemUnimport(endpoint, recvMemDesc.data(), static_cast<uint32_t>(recvMemDesc.size()));
    }
    if (sendBuffer != nullptr) (void)aclrtFree(sendBuffer);
    if (stream != nullptr) (void)aclrtDestroyStream(stream);
    if (thread != 0) (void)HcommThreadFree(&thread, 1);
    if (channel != 0) (void)HcommChannelDestroy(&channel, 1);
    if (endpoint != nullptr) (void)HcommEndpointDestroy(endpoint);
    if (fd >= 0) ::close(fd);
    (void)aclrtResetDevice(deviceId);
    (void)aclFinalize();
    return exitCode;
}
