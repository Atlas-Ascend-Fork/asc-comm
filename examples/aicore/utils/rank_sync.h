/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file rank_sync.h
 * \brief Ascend C样例共享的rank间同步工具。
 *
 * 以rank 0为中心的TCP星形控制通道，只提供纯粹的传输与同步原语，不感知也不解释
 * 传输的内容（HCCL建域等语义由上层组合，见hccl_comm_init.h）：
 *   - allgather（Allgather，等长载荷按rank序收集）；
 *   - barrier（Barrier，allgather的4字节特例，尽量简单）。
 *
 * endpoint一律由外部（命令行）传入，同时支持IPv4与IPv6：
 *   - IPv4格式："tcp://ip:port"，如 "tcp://127.0.0.1:29623"
 *   - IPv6格式："tcp://[ip]:port"，如 "tcp://[::1]:29623"
 * rank 0进程Listen绑定该地址，其余rank进程Connect连接。单机与跨机同一套代码
 * （单机传127.0.0.1或[::1]即可），并行运行多个实例时由用户显式错开端口。
 * 数据面同步（Drain/Flush/SynchronizeStream等设备侧语义）不在此处。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H
#define ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>

namespace examples {

// 控制通道消息头：tag区分集合调用点（同一调用点所有rank必须使用相同tag），
// len为单个rank的载荷长度（所有rank相同）。
struct RankSyncMsgHeader {
    uint32_t tag;
    uint32_t len;
};

// 集合调用点tag的分配基址。样例自定义tag请从kTagUserBase开始编号，保证同一样例内互不相同。
constexpr uint32_t kTagUserBase = 100U;

class RankSyncContext {
public:
    // rank 0进程调用：绑定endpoint并接受其余rank的连接。
    // 支持IPv4（"tcp://ip:port"）和IPv6（"tcp://[ip]:port"）格式。
    // bind失败（地址被占/不可达）时Ok()为false。
    static RankSyncContext Listen(const std::string& endpoint, uint32_t nranks)
    {
        RankSyncContext ctx(0U, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (ParseEndpoint(endpoint, host, port)) {
            fd = CreateListenSocket(host, port);
        }
        ctx.SetupServer(fd);
        return ctx;
    }

    // 客户端：连接endpoint指向的rank 0（连接失败按固定间隔重试）。
    // 支持IPv4（"tcp://ip:port"）和IPv6（"tcp://[ip]:port"）格式。
    static RankSyncContext Connect(uint32_t rank, uint32_t nranks, const std::string& endpoint)
    {
        RankSyncContext ctx(rank, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (ParseEndpoint(endpoint, host, port)) {
            fd = ConnectWithRetry(host, port);
            if (fd >= 0) {
                // 连接后先报身份，rank 0据此建立 fd -> rank 映射。
                const uint32_t self = rank;
                if (SendAll(fd, &self, sizeof(self))) {
                    ctx.peerFds_[0] = fd;
                    fd = -1; // 所有权已转移
                    ctx.ok_ = true;
                }
            }
        }
        if (fd >= 0) {
            close(fd);
        }
        return ctx;
    }

    RankSyncContext(const RankSyncContext&) = delete;
    RankSyncContext& operator=(const RankSyncContext&) = delete;
    RankSyncContext(RankSyncContext&& other) noexcept : RankSyncContext(other.rank_, other.nranks_)
    {
        ok_ = other.ok_;
        peerFds_ = std::move(other.peerFds_);
        listenFd_ = other.listenFd_;
        ownListenFd_ = other.ownListenFd_;
        other.ok_ = false;
        other.listenFd_ = -1;
        other.ownListenFd_ = false;
    }

    ~RankSyncContext()
    {
        for (int fd : peerFds_) {
            if (fd >= 0) {
                close(fd);
            }
        }
        if (listenFd_ >= 0 && ownListenFd_) {
            close(listenFd_);
        }
    }

    bool Ok() const { return ok_; }

    // 本rank的编号与通信域内rank总数。
    uint32_t Rank() const { return rank_; }
    uint32_t Nranks() const { return nranks_; }

    // 每个rank提供len字节，收集为nranks*len（按rank序写入recv）。所有rank必须在
    // 同一程序位置以相同tag调用，且len一致。
    bool Allgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        if (!ok_ || len == 0U) {
            return false;
        }
        if (rank_ == 0U) {
            return ServerAllgather(tag, send, len, recv);
        }
        return ClientAllgather(tag, send, len, recv);
    }

    // barrier：allgather的4字节特例。不同调用点用不同tag。
    bool Barrier(uint32_t tag)
    {
        uint32_t dummy = 0U;
        std::vector<uint32_t> all(nranks_, 0U);
        return Allgather(tag, &dummy, sizeof(dummy), all.data());
    }

private:
    explicit RankSyncContext(uint32_t rank, uint32_t nranks) : rank_(rank), nranks_(nranks), peerFds_(nranks, -1) {}

    static constexpr int kSockTimeoutSec = 60;
    static constexpr uint32_t kConnectRetryTimes = 300U;
    static constexpr uint32_t kConnectRetryIntervalMs = 100U;

    // 解析endpoint为host和port。支持两种格式：
    //   IPv4: "tcp://host:port"
    //   IPv6: "tcp://[host]:port"（RFC 3986方括号约定）
    static bool ParseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port)
    {
        constexpr const char* kPrefix = "tcp://";
        const size_t prefixLen = std::strlen(kPrefix);
        if (endpoint.rfind(kPrefix, 0) != 0) {
            return false;
        }
        const std::string remainder = endpoint.substr(prefixLen);
        if (remainder.empty()) {
            return false;
        }

        std::string portStr;
        if (remainder[0] == '[') {
            // IPv6格式：[host]:port
            const size_t closeBracket = remainder.find(']');
            if (closeBracket == std::string::npos || closeBracket < 2) {
                return false;
            }
            host = remainder.substr(1, closeBracket - 1);
            // ']'之后必须是':port'
            if (closeBracket + 1 >= remainder.size() || remainder[closeBracket + 1] != ':') {
                return false;
            }
            portStr = remainder.substr(closeBracket + 2);
        } else {
            // IPv4格式：host:port
            const size_t colon = remainder.rfind(':');
            if (colon == std::string::npos || colon == 0) {
                return false;
            }
            host = remainder.substr(0, colon);
            portStr = remainder.substr(colon + 1);
        }

        if (host.empty() || portStr.empty()) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(portStr.c_str(), &end, 10);
        if (errno != 0 || end == portStr.c_str() || *end != '\0' || value <= 0 || value > 65535L) {
            return false;
        }
        port = static_cast<uint16_t>(value);
        return true;
    }

    static bool SetSockTimeouts(int fd)
    {
        struct timeval tv = {};
        tv.tv_sec = kSockTimeoutSec;
        return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
               setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
    }

    static bool SendAll(int fd, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        size_t sent = 0U;
        while (sent < size) {
            const ssize_t n = send(fd, bytes + sent, size - sent, 0);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    static bool RecvAll(int fd, void* data, size_t size)
    {
        auto* bytes = static_cast<uint8_t*>(data);
        size_t received = 0U;
        while (received < size) {
            const ssize_t n = recv(fd, bytes + received, size - received, 0);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return false;
            }
            received += static_cast<size_t>(n);
        }
        return true;
    }

    // 使用getaddrinfo解析地址并创建监听socket，同时支持IPv4和IPv6。
    // 遍历解析结果，尝试每个地址直到bind+listen成功。
    static int CreateListenSocket(const std::string& host, uint16_t port)
    {
        const std::string portStr = std::to_string(port);
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC; // 同时尝试IPv4和IPv6
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE; // 用于bind
        struct addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            return -1;
        }
        int fd = -1;
        for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                continue;
            }
            const int reuse = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            // IPv6时关闭IPv4映射，确保独立监听IPv6地址。
            if (rp->ai_family == AF_INET6) {
                const int v6only = 1;
                (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
            }
            if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(fd, 64) == 0) {
                break; // 成功
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(result);
        return fd;
    }

    // 使用getaddrinfo解析地址并连接，同时支持IPv4和IPv6。
    // rank 0可能尚未开始accept（逐rank部署时启动有先后），按固定间隔重试。
    static int ConnectWithRetry(const std::string& host, uint16_t port)
    {
        const std::string portStr = std::to_string(port);
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            return -1;
        }
        int fd = -1;
        for (uint32_t attempt = 0U; attempt < kConnectRetryTimes; ++attempt) {
            for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
                fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (fd < 0) {
                    continue;
                }
                if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
                    if (SetSockTimeouts(fd)) {
                        freeaddrinfo(result);
                        return fd;
                    }
                    close(fd);
                    freeaddrinfo(result);
                    return -1;
                }
                close(fd);
                fd = -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kConnectRetryIntervalMs));
        }
        freeaddrinfo(result);
        return -1;
    }

    // listenFd为Listen()绑定好的监听socket，本上下文拥有并在析构时关闭。
    // listenFd<0表示构造失败。依次接受其余rank的连接；连接后对方先发送自己的rank号。
    void SetupServer(int listenFd)
    {
        if (listenFd < 0) {
            return;
        }
        listenFd_ = listenFd;
        ownListenFd_ = true;
        for (uint32_t accepted = 0U; accepted + 1U < nranks_; ++accepted) {
            int fd = -1;
            do {
                fd = accept(listenFd, nullptr, nullptr);
            } while (fd < 0 && errno == EINTR);
            if (fd < 0 || !SetSockTimeouts(fd)) {
                if (fd >= 0) {
                    close(fd);
                }
                return;
            }
            uint32_t peerRank = 0U;
            if (!RecvAll(fd, &peerRank, sizeof(peerRank)) || peerRank == 0U || peerRank >= nranks_ ||
                peerFds_[peerRank] >= 0) {
                close(fd);
                return;
            }
            peerFds_[peerRank] = fd;
        }
        ok_ = true;
    }

    bool ServerAllgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        auto* out = static_cast<uint8_t*>(recv);
        std::memcpy(out, send, len); // slot 0：rank 0自己的载荷
        for (uint32_t peer = 1U; peer < nranks_; ++peer) {
            RankSyncMsgHeader header = {};
            if (!RecvAll(peerFds_[peer], &header, sizeof(header)) || header.tag != tag || header.len != len) {
                return false;
            }
            if (!RecvAll(peerFds_[peer], out + static_cast<size_t>(peer) * len, len)) {
                return false;
            }
        }
        // 全员数据到齐后回发整表。
        for (uint32_t peer = 1U; peer < nranks_; ++peer) {
            const RankSyncMsgHeader reply = {tag, nranks_ * len};
            if (!SendAll(peerFds_[peer], &reply, sizeof(reply)) ||
                !SendAll(peerFds_[peer], out, static_cast<size_t>(nranks_) * len)) {
                return false;
            }
        }
        return true;
    }

    bool ClientAllgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        const RankSyncMsgHeader header = {tag, len};
        if (!SendAll(peerFds_[0], &header, sizeof(header)) || !SendAll(peerFds_[0], send, len)) {
            return false;
        }
        RankSyncMsgHeader reply = {};
        if (!RecvAll(peerFds_[0], &reply, sizeof(reply)) || reply.tag != tag || reply.len != nranks_ * len) {
            return false;
        }
        return RecvAll(peerFds_[0], recv, static_cast<size_t>(nranks_) * len);
    }

    uint32_t rank_;
    uint32_t nranks_;
    // peerFds_[rank]是与该rank的连接；rank 0侧slot 0未用（自己），客户端侧只有slot 0。
    std::vector<int> peerFds_;
    int listenFd_ = -1;
    bool ownListenFd_ = false;
    bool ok_ = false;
};

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H
