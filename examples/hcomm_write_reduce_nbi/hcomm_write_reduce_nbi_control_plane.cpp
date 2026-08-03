// Copyright (c) Huawei Technologies Co., Ltd.
//
// 基于 RootInfo 建链的两卡 WriteReduceNbi 样例 host 控制面。

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_comm.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_common.h"

#define HCCLCHECK(ret)                                                                          \
    do {                                                                                        \
        const HcclResult checkRet = (ret);                                                      \
        if (checkRet != HCCL_SUCCESS) {                                                         \
            std::fprintf(stderr, "%s:%d: %s return %d.\n", __FILE__, __LINE__, #ret, checkRet); \
            return checkRet;                                                                    \
        }                                                                                       \
    } while (0)

#define ACLCHECK(ret)                                                                           \
    do {                                                                                        \
        const aclError checkRet = (ret);                                                        \
        if (checkRet != ACL_SUCCESS) {                                                          \
            std::fprintf(stderr, "%s:%d: %s return %d.\n", __FILE__, __LINE__, #ret, checkRet); \
            return HCCL_E_RUNTIME;                                                              \
        }                                                                                       \
    } while (0)

extern "C" void run_hcomm_single_jetty_multi_peer(
    ChannelHandle channel, uint8_t* localAddr, uint8_t* remoteAddr, uint8_t* remoteNotifyAddr, uint64_t count,
    uint64_t* cycleOut, uint32_t dataType, uint32_t reduceOp, uint32_t warmupTimes, uint32_t measureTimes,
    aclrtStream stream);

namespace {

constexpr const char* RECV_MEM_TAG = "recv_buffer";
constexpr const char* DEFAULT_ROOT_INFO_IP_PORT = "tcp://127.0.0.1:8899";
constexpr int ROOT_INFO_CONNECT_RETRY_TIMES = 300;
constexpr int ROOT_INFO_CONNECT_RETRY_INTERVAL_MS = 100;
constexpr uint64_t NOTIFY_VALUE = 1;
constexpr uint64_t NOTIFY_BYTES = sizeof(uint64_t);
constexpr uint32_t NOTIFY_WAIT_TIMEOUT_MS = 30000;
constexpr uint32_t NOTIFY_POLL_INTERVAL_MS = 1;
constexpr uint32_t WARMUP_TIMES = 0;
constexpr uint32_t MEASURE_TIMES = 1;

struct DataTypeConfig {
    const char* name;
    AscendC::HcommDataType value;
    uint32_t size;
};

struct ReduceOpConfig {
    const char* name;
    AscendC::HcommReduceOp value;
};

struct TestValues {
    uint32_t initial;
    uint32_t send[2];
};

constexpr DataTypeConfig DATA_TYPES[] = {
    {"INT8", AscendC::HCOMM_DATA_TYPE_INT8, sizeof(int8_t)},
    {"INT16", AscendC::HCOMM_DATA_TYPE_INT16, sizeof(int16_t)},
    {"INT32", AscendC::HCOMM_DATA_TYPE_INT32, sizeof(int32_t)},
    {"UINT32", AscendC::HCOMM_DATA_TYPE_UINT32, sizeof(uint32_t)},
    {"FP16", AscendC::HCOMM_DATA_TYPE_FP16, sizeof(uint16_t)},
    {"FP32", AscendC::HCOMM_DATA_TYPE_FP32, sizeof(float)},
    {"BFP16", AscendC::HCOMM_DATA_TYPE_BFP16, sizeof(uint16_t)},
};

constexpr ReduceOpConfig REDUCE_OPS[] = {
    {"MAX", AscendC::HCOMM_REDUCE_MAX},
    {"MIN", AscendC::HCOMM_REDUCE_MIN},
    {"SUM", AscendC::HCOMM_REDUCE_SUM},
};

const DataTypeConfig* FindDataType(const char* name)
{
    for (const auto& dataType : DATA_TYPES) {
        if (std::strcmp(name, dataType.name) == 0) {
            return &dataType;
        }
    }
    return nullptr;
}

const ReduceOpConfig* FindReduceOp(const char* name)
{
    for (const auto& reduceOp : REDUCE_OPS) {
        if (std::strcmp(name, reduceOp.name) == 0) {
            return &reduceOp;
        }
    }
    return nullptr;
}

TestValues GetTestValues(AscendC::HcommReduceOp reduceOp)
{
    if (reduceOp == AscendC::HCOMM_REDUCE_MAX) {
        return {1U, {2U, 3U}};
    }
    if (reduceOp == AscendC::HCOMM_REDUCE_MIN) {
        return {3U, {1U, 2U}};
    }
    return {1U, {1U, 1U}};
}

HcclResult CalculateExpectedValue(
    uint32_t initialValue, uint32_t peerValue, uint32_t reduceTimes, AscendC::HcommReduceOp reduceOp,
    uint32_t* expected)
{
    if (expected == nullptr) {
        return HCCL_E_PTR;
    }

    *expected = initialValue;
    for (uint32_t i = 0; i < reduceTimes; ++i) {
        if (reduceOp == AscendC::HCOMM_REDUCE_MAX) {
            *expected = std::max(*expected, peerValue);
        } else if (reduceOp == AscendC::HCOMM_REDUCE_MIN) {
            *expected = std::min(*expected, peerValue);
        } else {
            *expected += peerValue;
        }
    }
    return HCCL_SUCCESS;
}

bool ParseIpPort(const std::string& ipPort, std::string* host, uint16_t* port)
{
    if (host == nullptr || port == nullptr) {
        return false;
    }

    constexpr const char* TCP_PREFIX = "tcp://";
    std::string value = ipPort;
    if (value.compare(0, std::strlen(TCP_PREFIX), TCP_PREFIX) == 0) {
        value = value.substr(std::strlen(TCP_PREFIX));
    }

    const std::size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        return false;
    }

    const std::string portText = value.substr(separator + 1);
    char* end = nullptr;
    errno = 0;
    const long parsedPort = std::strtol(portText.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0' || parsedPort <= 0 || parsedPort > 65535) {
        return false;
    }

    *host = value.substr(0, separator);
    *port = static_cast<uint16_t>(parsedPort);
    return true;
}

bool SendAll(int fd, const void* data, std::size_t size)
{
    const auto* buffer = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t count = send(fd, buffer + sent, size - sent, 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool RecvAll(int fd, void* data, std::size_t size)
{
    auto* buffer = static_cast<char*>(data);
    std::size_t received = 0;
    while (received < size) {
        const ssize_t count = recv(fd, buffer + received, size - received, 0);
        if (count <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(count);
    }
    return true;
}

int CreateListenSocket(const std::string& host, uint16_t port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "socket failed, errno=%d.\n", errno);
        return -1;
    }

    int reuseAddress = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "bind %s:%u failed, errno=%d.\n", host.c_str(), port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 64) != 0) {
        std::fprintf(stderr, "listen failed, errno=%d.\n", errno);
        close(fd);
        return -1;
    }
    return fd;
}

int ConnectToRoot(const std::string& host, uint16_t port)
{
    for (int retry = 0; retry < ROOT_INFO_CONNECT_RETRY_TIMES; ++retry) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            hostent* hostEntry = gethostbyname(host.c_str());
            if (hostEntry == nullptr || hostEntry->h_addr_list == nullptr || hostEntry->h_addr_list[0] == nullptr) {
                close(fd);
                return -1;
            }
            std::memcpy(&address.sin_addr, hostEntry->h_addr_list[0], sizeof(address.sin_addr));
        }

        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            return fd;
        }
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(ROOT_INFO_CONNECT_RETRY_INTERVAL_MS));
    }
    return -1;
}

HcclResult ExchangeRootInfo(uint32_t rank, uint32_t rankSize, const std::string& ipPort, HcclRootInfo* rootInfo)
{
    if (rootInfo == nullptr) {
        return HCCL_E_PTR;
    }

    std::string host;
    uint16_t port = 0;
    if (!ParseIpPort(ipPort, &host, &port)) {
        std::fprintf(stderr, "invalid ipport: %s.\n", ipPort.c_str());
        return HCCL_E_PARA;
    }

    if (rank == 0) {
        const int listenFd = CreateListenSocket(host, port);
        if (listenFd < 0) {
            return HCCL_E_INTERNAL;
        }

        for (uint32_t peer = 1; peer < rankSize; ++peer) {
            const int connectionFd = accept(listenFd, nullptr, nullptr);
            if (connectionFd < 0) {
                std::fprintf(stderr, "accept failed, errno=%d.\n", errno);
                close(listenFd);
                return HCCL_E_INTERNAL;
            }
            if (!SendAll(connectionFd, rootInfo, sizeof(*rootInfo))) {
                std::fprintf(stderr, "send root info failed.\n");
                close(connectionFd);
                close(listenFd);
                return HCCL_E_INTERNAL;
            }
            close(connectionFd);
        }
        close(listenFd);
        return HCCL_SUCCESS;
    }

    const int fd = ConnectToRoot(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "connect ipport failed: %s.\n", ipPort.c_str());
        return HCCL_E_INTERNAL;
    }
    if (!RecvAll(fd, rootInfo, sizeof(*rootInfo))) {
        std::fprintf(stderr, "receive root info failed.\n");
        close(fd);
        return HCCL_E_INTERNAL;
    }
    close(fd);
    return HCCL_SUCCESS;
}

const CommLink* FindUbcCtpLink(const CommLink* links, uint32_t linkNum)
{
    for (uint32_t i = 0; i < linkNum; ++i) {
        if (links[i].linkAttr.linkProtocol == COMM_PROTOCOL_UBC_CTP) {
            return &links[i];
        }
    }
    return nullptr;
}

HcclResult AcquirePeerChannel(HcclComm comm, uint32_t selfRank, uint32_t peerRank, HcclMemHandle* memHandles,
    uint32_t memHandleNum, ChannelHandle* channel)
{
    if (channel == nullptr) {
        return HCCL_E_PTR;
    }

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));

    for (uint32_t layerIndex = 0; layerIndex < layerNum; ++layerIndex) {
        uint32_t* ranks = nullptr;
        uint32_t rankNum = 0;
        HCCLCHECK(HcclRankGraphGetRanksByLayer(comm, layers[layerIndex], &ranks, &rankNum));

        bool peerInLayer = false;
        for (uint32_t rankIndex = 0; rankIndex < rankNum; ++rankIndex) {
            if (ranks[rankIndex] == peerRank) {
                peerInLayer = true;
                break;
            }
        }
        if (!peerInLayer) {
            continue;
        }

        CommLink* links = nullptr;
        uint32_t linkNum = 0;
        HCCLCHECK(HcclRankGraphGetLinks(comm, layers[layerIndex], selfRank, peerRank, &links, &linkNum));

        const CommLink* link = FindUbcCtpLink(links, linkNum);
        if (link == nullptr) {
            continue;
        }

        HcclChannelDesc desc;
        HCCLCHECK(HcclChannelDescInit(&desc, 1));
        desc.remoteRank = peerRank;
        desc.channelProtocol = link->linkAttr.linkProtocol;
        desc.localEndpoint = link->srcEndpointDesc;
        desc.remoteEndpoint = link->dstEndpointDesc;
        desc.memHandles = memHandles;
        desc.memHandleNum = memHandleNum;
        HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &desc, 1, channel));

        std::printf("rank %u: acquired one AIV channel to rank %u.\n", selfRank, peerRank);
        return HCCL_SUCCESS;
    }

    std::fprintf(stderr, "rank %u: UBC CTP link to rank %u not found.\n", selfRank, peerRank);
    return HCCL_E_NOT_FOUND;
}

uint16_t EncodePositiveIntegerAsFp16(uint32_t value)
{
    if (value == 0U) {
        return 0U;
    }
    uint32_t exponent = 0;
    for (uint32_t normalized = value; normalized > 1U; normalized >>= 1U) {
        ++exponent;
    }
    constexpr uint32_t FP16_EXPONENT_BIAS = 15U;
    constexpr uint32_t FP16_MANTISSA_BITS = 10U;
    uint32_t mantissa = (value - (1U << exponent)) << (FP16_MANTISSA_BITS - exponent);
    return static_cast<uint16_t>(((exponent + FP16_EXPONENT_BIAS) << FP16_MANTISSA_BITS) | mantissa);
}

bool EncodeScalar(const DataTypeConfig& dataType, uint32_t value, uint8_t* output)
{
    if (output == nullptr) {
        return false;
    }
    std::memset(output, 0, sizeof(uint32_t));
    switch (dataType.value) {
        case AscendC::HCOMM_DATA_TYPE_INT8: {
            int8_t encoded = static_cast<int8_t>(value);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_INT16: {
            int16_t encoded = static_cast<int16_t>(value);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_INT32: {
            int32_t encoded = static_cast<int32_t>(value);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_UINT32: {
            uint32_t encoded = value;
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_FP16: {
            uint16_t encoded = EncodePositiveIntegerAsFp16(value);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_FP32: {
            float encoded = static_cast<float>(value);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        case AscendC::HCOMM_DATA_TYPE_BFP16: {
            float fp32 = static_cast<float>(value);
            uint32_t fp32Bits = 0;
            std::memcpy(&fp32Bits, &fp32, sizeof(fp32Bits));
            uint16_t encoded = static_cast<uint16_t>(fp32Bits >> 16U);
            std::memcpy(output, &encoded, sizeof(encoded));
            return true;
        }
        default:
            return false;
    }
}

HcclResult FillDeviceBuffer(void* buffer, uint64_t bytes, const DataTypeConfig& dataType, uint32_t value)
{
    if (buffer == nullptr || bytes % dataType.size != 0U) {
        return HCCL_E_PARA;
    }
    uint8_t encoded[sizeof(uint32_t)]{};
    if (!EncodeScalar(dataType, value, encoded)) {
        return HCCL_E_PARA;
    }
    std::vector<uint8_t> host(bytes, 0U);
    for (uint64_t offset = 0; offset < bytes; offset += dataType.size) {
        std::memcpy(host.data() + offset, encoded, dataType.size);
    }
    ACLCHECK(aclrtMemcpy(buffer, bytes, host.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE));
    return HCCL_SUCCESS;
}

HcclResult WaitForNotifyValue(void* notifyAddr, uint64_t expected)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(NOTIFY_WAIT_TIMEOUT_MS);
    uint64_t value = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        ACLCHECK(aclrtMemcpy(&value, sizeof(value), notifyAddr, sizeof(value), ACL_MEMCPY_DEVICE_TO_HOST));
        if (value >= expected) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTIFY_POLL_INTERVAL_MS));
    }
    std::fprintf(
        stderr, "wait for incoming Hcomm notify timed out, expected=%llu, actual=%llu.\n",
        static_cast<unsigned long long>(expected), static_cast<unsigned long long>(value));
    return HCCL_E_TIMEOUT;
}

HcclResult VerifyDeviceBuffer(
    void* buffer, uint64_t bytes, const DataTypeConfig& dataType, uint32_t expected, const char* caseName)
{
    if (buffer == nullptr || caseName == nullptr || bytes % dataType.size != 0U) {
        return HCCL_E_PARA;
    }
    uint8_t encoded[sizeof(uint32_t)]{};
    if (!EncodeScalar(dataType, expected, encoded)) {
        return HCCL_E_PARA;
    }
    std::vector<uint8_t> host(bytes, 0U);
    ACLCHECK(aclrtMemcpy(host.data(), bytes, buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST));

    uint32_t expectedBits = 0;
    std::memcpy(&expectedBits, encoded, dataType.size);
    for (uint64_t offset = 0; offset < bytes; offset += dataType.size) {
        if (std::memcmp(host.data() + offset, encoded, dataType.size) != 0) {
            uint32_t actualBits = 0;
            std::memcpy(&actualBits, host.data() + offset, dataType.size);
            std::fprintf(stderr, "verify %s failed at element %llu, expected bits 0x%x, actual bits 0x%x.\n",
                caseName, static_cast<unsigned long long>(offset / dataType.size), expectedBits, actualBits);
            return HCCL_E_INTERNAL;
        }
    }
    return HCCL_SUCCESS;
}

HcclResult GetRemoteMemByTag(
    HcclComm comm, ChannelHandle channel, const char* memTag, uint64_t minBytes, uint8_t** remoteAddr)
{
    if (remoteAddr == nullptr) {
        return HCCL_E_PTR;
    }
    *remoteAddr = nullptr;

    uint32_t memNum = 0;
    CommMem* remoteMems = nullptr;
    char** memTags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags));

    for (uint32_t i = 0; i < memNum; ++i) {
        if (memTags[i] != nullptr && std::strcmp(memTags[i], memTag) == 0) {
            if (remoteMems[i].addr == nullptr || remoteMems[i].size < minBytes) {
                return HCCL_E_PARA;
            }
            *remoteAddr = static_cast<uint8_t*>(remoteMems[i].addr);
            return HCCL_SUCCESS;
        }
    }

    std::fprintf(stderr, "remote mem tag %s not found.\n", memTag);
    return HCCL_E_NOT_FOUND;
}

HcclResult RunTransfer(HcclComm comm, ChannelHandle channel, uint8_t* localAddr, uint8_t* localNotifyAddr,
    uint64_t remoteMemBytes, uint64_t count, uint32_t dataType, uint32_t reduceOp, uint64_t* cycleAddr,
    uint64_t* totalCycles, aclrtStream stream)
{
    if (cycleAddr == nullptr || totalCycles == nullptr) {
        return HCCL_E_PTR;
    }

    uint8_t* remoteAddr = nullptr;
    HCCLCHECK(GetRemoteMemByTag(comm, channel, RECV_MEM_TAG, remoteMemBytes, &remoteAddr));
    uint8_t* remoteNotifyAddr = remoteAddr + remoteMemBytes - NOTIFY_BYTES;
    run_hcomm_single_jetty_multi_peer(channel, localAddr, remoteAddr, remoteNotifyAddr, count, cycleAddr, dataType,
        reduceOp, WARMUP_TIMES, MEASURE_TIMES, stream);
    ACLCHECK(aclrtSynchronizeStream(stream));
    ACLCHECK(aclrtMemcpy(
        totalCycles, sizeof(*totalCycles), cycleAddr, sizeof(*totalCycles), ACL_MEMCPY_DEVICE_TO_HOST));
    return WaitForNotifyValue(localNotifyAddr, NOTIFY_VALUE);
}

HcclResult RunSample(uint32_t rankSize, uint32_t rank, uint32_t device, uint64_t bytes,
    const DataTypeConfig& dataType, const ReduceOpConfig& reduceOp, const std::string& ipPort)
{
    if (rankSize != 2 || rank >= rankSize) {
        return HCCL_E_PARA;
    }

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    HcclRootInfo rootInfo{};
    if (rank == 0) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
    }
    HCCLCHECK(ExchangeRootInfo(rank, rankSize, ipPort, &rootInfo));

    HcclComm comm = nullptr;
    const HcclResult initRet = HcclCommInitRootInfo(rankSize, &rootInfo, rank, &comm);
    if (initRet != HCCL_SUCCESS) {
        std::fprintf(stderr, "rank %u: HcclCommInitRootInfo failed, ret=%d.\n", rank, initRet);
        return initRet;
    }

    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    const uint64_t alignedBytes = (bytes + NOTIFY_BYTES - 1) / NOTIFY_BYTES * NOTIFY_BYTES;
    const uint64_t remoteMemBytes = alignedBytes + NOTIFY_BYTES;
    const uint64_t cycleBytes = sizeof(uint64_t);
    const uint64_t commBufferBytes = alignedBytes + remoteMemBytes + cycleBytes;
    void* commBufferAddr = nullptr;
    ACLCHECK(aclrtMalloc(&commBufferAddr, commBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));

    auto* sendAddr = static_cast<uint8_t*>(commBufferAddr);
    auto* recvAddr = sendAddr + alignedBytes;
    auto* recvNotifyAddr = recvAddr + alignedBytes;
    auto* cycleAddr = reinterpret_cast<uint64_t*>(recvNotifyAddr + NOTIFY_BYTES);
    const TestValues testValues = GetTestValues(reduceOp.value);
    HCCLCHECK(FillDeviceBuffer(sendAddr, alignedBytes, dataType, testValues.send[rank]));
    HCCLCHECK(FillDeviceBuffer(recvAddr, alignedBytes, dataType, testValues.initial));
    ACLCHECK(aclrtMemset(recvNotifyAddr, NOTIFY_BYTES, 0, NOTIFY_BYTES));
    ACLCHECK(aclrtMemset(cycleAddr, cycleBytes, 0, cycleBytes));

    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvAddr, remoteMemBytes};
    HcclMemHandle recvMemHandle = nullptr;
    HCCLCHECK(HcclCommMemReg(comm, RECV_MEM_TAG, &recvMem, &recvMemHandle));

    const uint32_t peer = 1 - rank;
    ChannelHandle channel = 0;
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, &recvMemHandle, 1, &channel));
    uint64_t totalCycles = 0;
    HCCLCHECK(RunTransfer(comm, channel, sendAddr, recvNotifyAddr, remoteMemBytes, bytes / dataType.size,
        static_cast<uint32_t>(dataType.value), static_cast<uint32_t>(reduceOp.value), cycleAddr, &totalCycles, stream));

    uint32_t expected = 0;
    HCCLCHECK(CalculateExpectedValue(testValues.initial, testValues.send[peer], WARMUP_TIMES + MEASURE_TIMES,
        reduceOp.value, &expected));
    const std::string caseName = std::string(reduceOp.name) + " + " + dataType.name;
    HCCLCHECK(VerifyDeviceBuffer(recvAddr, bytes, dataType, expected, caseName.c_str()));

    const double averageCycles = static_cast<double>(totalCycles) / MEASURE_TIMES;

    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(commBufferAddr));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(static_cast<int32_t>(device)));
    ACLCHECK(aclFinalize());

    std::printf("rank %u pass, peer %u, case %s, bytes %llu, WriteReduceNbi<false> cycles: total %llu, "
                "average %.2f.\n",
        rank, peer, caseName.c_str(), static_cast<unsigned long long>(bytes),
        static_cast<unsigned long long>(totalCycles), averageCycles);
    return HCCL_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 7 && argc != 8) {
        std::fprintf(stderr,
            "Usage: %s <rank_size> <rank> <device> <bytes> <data_type> <reduce_op> [ipport]\n", argv[0]);
        return 1;
    }

    int argIndex = 1;
    const int rankSizeArg = std::atoi(argv[argIndex++]);
    const int rankArg = std::atoi(argv[argIndex++]);
    const int deviceArg = std::atoi(argv[argIndex++]);
    const uint64_t bytes = static_cast<uint64_t>(std::strtoull(argv[argIndex++], nullptr, 10));
    const DataTypeConfig* dataType = FindDataType(argv[argIndex++]);
    const ReduceOpConfig* reduceOp = FindReduceOp(argv[argIndex++]);
    const std::string ipPort = argc == 8 ? argv[argIndex] : DEFAULT_ROOT_INFO_IP_PORT;

    if (rankSizeArg != 2 || rankArg < 0 || rankArg >= rankSizeArg || deviceArg < 0 || bytes == 0 ||
        dataType == nullptr || reduceOp == nullptr || bytes % dataType->size != 0U) {
        std::fprintf(stderr,
            "invalid arguments: use 2 ranks, a supported data type/op, and data-type-aligned bytes.\n");
        return 1;
    }

    const uint32_t rankSize = static_cast<uint32_t>(rankSizeArg);
    const uint32_t rank = static_cast<uint32_t>(rankArg);
    const uint32_t device = static_cast<uint32_t>(deviceArg);
    const HcclResult ret = RunSample(rankSize, rank, device, bytes, *dataType, *reduceOp, ipPort);
    if (ret != HCCL_SUCCESS) {
        std::fprintf(stderr, "sample failed, ret %d.\n", ret);
        return static_cast<int>(ret);
    }
    return 0;
}
