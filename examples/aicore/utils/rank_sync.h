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
 * endpoint一律由外部（命令行）传入，格式"tcp://ip:port"：rank 0进程Listen绑定该地址，
 * 其余rank进程Connect连接。单机与跨机同一套代码（单机传127.0.0.1即可），并行运行多个
 * 实例时由用户显式错开端口。数据面同步（Drain/Flush/SynchronizeStream等设备侧语义）
 * 不在此处。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H
#define ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    // rank 0进程调用：绑定endpoint（"tcp://ip:port"）并接受其余rank的连接。
    // bind失败（地址被占/不可达）时Ok()为false。
    static RankSyncContext Listen(const std::string& endpoint, uint32_t nranks)
    {
        RankSyncContext ctx(0U, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (ParseEndpoint(endpoint, host, port)) {
            fd = CreateListenSocket(host, port);
            if (fd < 0) {
                ctx.Log("listen failed: bind %s:%u, errno=%d", host.c_str(), port, errno);
            }
        } else {
            ctx.Log("invalid endpoint '%s' (expect tcp://ip:port)", endpoint.c_str());
        }
        ctx.SetupServer(fd);
        return ctx;
    }

    // 客户端：连接endpoint指向的rank 0（连接失败按固定间隔重试）。
    static RankSyncContext Connect(uint32_t rank, uint32_t nranks, const std::string& endpoint)
    {
        RankSyncContext ctx(rank, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (ParseEndpoint(endpoint, host, port)) {
            fd = ConnectWithRetry(host, port);
            if (fd < 0) {
                ctx.Log(
                    "connect to %s:%u failed after %u retries (%.0f s)", host.c_str(), port, kConnectRetryTimes,
                    kConnectRetryTimes * kConnectRetryIntervalMs / 1000.0);
            } else {
                // 连接后先报身份，rank 0据此建立 fd -> rank 映射。
                const uint32_t self = rank;
                if (SendAll(fd, &self, sizeof(self))) {
                    ctx.peerFds_[0] = fd;
                    fd = -1; // 所有权已转移
                    ctx.ok_ = true;
                    ctx.Log("connected to rank 0 at %s:%u", host.c_str(), port);
                } else {
                    ctx.Log("send rank id to rank 0 failed, errno=%d", errno);
                }
            }
        } else {
            ctx.Log("invalid endpoint '%s' (expect tcp://ip:port)", endpoint.c_str());
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
            Log("allgather tag=%u rejected: ok=%d len=%u", tag, static_cast<int>(ok_), len);
            return false;
        }
        if (rank_ == 0U) {
            Log("allgather tag=%u len=%u start (server, nranks=%u)", tag, len, nranks_);
            const bool ok = ServerAllgather(tag, send, len, recv);
            Log("allgather tag=%u len=%u %s (server)", tag, len, ok ? "done" : "FAILED");
            return ok;
        }
        Log("allgather tag=%u len=%u start (client)", tag, len);
        const bool ok = ClientAllgather(tag, send, len, recv);
        Log("allgather tag=%u len=%u %s (client)", tag, len, ok ? "done" : "FAILED");
        return ok;
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

    // 排查日志：统一前缀[RANKSYNC][rank N]打到stderr。默认开启，便于定位建链/集合
    // 通信问题；定义RANK_SYNC_QUIET可关闭。
    void Log(const char* fmt, ...) const
    {
#ifdef RANK_SYNC_QUIET
        (void)fmt;
#else
        std::fprintf(stderr, "[RANKSYNC][rank %u] ", rank_);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(stderr, fmt, args);
        va_end(args);
        std::fprintf(stderr, "\n");
#endif
    }

    static bool ParseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port)
    {
        constexpr const char* kPrefix = "tcp://";
        const size_t prefixLen = std::strlen(kPrefix);
        if (endpoint.rfind(kPrefix, 0) != 0) {
            return false;
        }
        const size_t colon = endpoint.rfind(':');
        if (colon == std::string::npos || colon < prefixLen) {
            return false;
        }
        host = endpoint.substr(prefixLen, colon - prefixLen);
        const long value = std::strtol(endpoint.c_str() + colon + 1, nullptr, 10);
        if (host.empty() || value <= 0 || value > 65535L) {
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

    static int CreateListenSocket(const std::string& host, uint16_t port)
    {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        const int reuse = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            close(fd);
            return -1;
        }
        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 || listen(fd, 64) != 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    static int ConnectWithRetry(const std::string& host, uint16_t port)
    {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            return -1;
        }
        for (uint32_t attempt = 0U; attempt < kConnectRetryTimes; ++attempt) {
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                return -1;
            }
            if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                if (SetSockTimeouts(fd)) {
                    return fd;
                }
                close(fd);
                return -1;
            }
            close(fd);
            // rank 0可能尚未开始accept（逐rank部署时启动有先后），按固定间隔重试。
            std::this_thread::sleep_for(std::chrono::milliseconds(kConnectRetryIntervalMs));
        }
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
            if (fd < 0) {
                Log("accept failed after %u/%u peers, errno=%d", accepted, nranks_ - 1U, errno);
                return;
            }
            if (!SetSockTimeouts(fd)) {
                Log("setsockopt on accepted fd failed, errno=%d", errno);
                close(fd);
                return;
            }
            uint32_t peerRank = 0U;
            if (!RecvAll(fd, &peerRank, sizeof(peerRank))) {
                Log("recv rank id from new connection failed, errno=%d", errno);
                close(fd);
                return;
            }
            if (peerRank == 0U || peerRank >= nranks_) {
                Log("invalid peer rank %u (nranks=%u) from new connection", peerRank, nranks_);
                close(fd);
                return;
            }
            if (peerFds_[peerRank] >= 0) {
                Log("duplicate connection from rank %u", peerRank);
                close(fd);
                return;
            }
            peerFds_[peerRank] = fd;
            Log("accepted rank %u (%u/%u)", peerRank, accepted + 1U, nranks_ - 1U);
        }
        ok_ = true;
        Log("server ready: all %u peers connected", nranks_ - 1U);
    }

    bool ServerAllgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        auto* out = static_cast<uint8_t*>(recv);
        std::memcpy(out, send, len); // slot 0：rank 0自己的载荷
        for (uint32_t peer = 1U; peer < nranks_; ++peer) {
            RankSyncMsgHeader header = {};
            if (peerFds_[peer] < 0) {
                Log("allgather tag=%u: no connection to rank %u (not accepted?)", tag, peer);
                return false;
            }
            if (!RecvAll(peerFds_[peer], &header, sizeof(header))) {
                Log("allgather tag=%u: recv header from rank %u failed (peer closed/timeout, errno=%d)", tag, peer,
                    errno);
                return false;
            }
            if (header.tag != tag || header.len != len) {
                Log("allgather tag=%u: header mismatch from rank %u, got tag=%u len=%u (expect tag=%u len=%u) — "
                    "ranks disagree on call order or payload size",
                    tag, peer, header.tag, header.len, tag, len);
                return false;
            }
            if (!RecvAll(peerFds_[peer], out + static_cast<size_t>(peer) * len, len)) {
                Log("allgather tag=%u: recv payload from rank %u failed (peer closed/timeout, errno=%d)", tag, peer,
                    errno);
                return false;
            }
        }
        // 全员数据到齐后回发整表。
        for (uint32_t peer = 1U; peer < nranks_; ++peer) {
            const RankSyncMsgHeader reply = {tag, nranks_ * len};
            if (!SendAll(peerFds_[peer], &reply, sizeof(reply))) {
                Log("allgather tag=%u: send reply header to rank %u failed (errno=%d)", tag, peer, errno);
                return false;
            }
            if (!SendAll(peerFds_[peer], out, static_cast<size_t>(nranks_) * len)) {
                Log("allgather tag=%u: send reply payload to rank %u failed (errno=%d)", tag, peer, errno);
                return false;
            }
        }
        return true;
    }

    bool ClientAllgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        if (peerFds_[0] < 0) {
            Log("allgather tag=%u: no connection to rank 0 (connect failed?)", tag);
            return false;
        }
        const RankSyncMsgHeader header = {tag, len};
        if (!SendAll(peerFds_[0], &header, sizeof(header))) {
            Log("allgather tag=%u: send header to rank 0 failed (errno=%d)", tag, errno);
            return false;
        }
        if (!SendAll(peerFds_[0], send, len)) {
            Log("allgather tag=%u: send payload to rank 0 failed (errno=%d)", tag, errno);
            return false;
        }
        RankSyncMsgHeader reply = {};
        if (!RecvAll(peerFds_[0], &reply, sizeof(reply))) {
            Log("allgather tag=%u: recv reply header failed (rank 0 closed/timeout, errno=%d)", tag, errno);
            return false;
        }
        if (reply.tag != tag || reply.len != nranks_ * len) {
            Log("allgather tag=%u: reply mismatch, got tag=%u len=%u (expect tag=%u len=%u)", tag, reply.tag, reply.len,
                tag, nranks_ * len);
            return false;
        }
        if (!RecvAll(peerFds_[0], recv, static_cast<size_t>(nranks_) * len)) {
            Log("allgather tag=%u: recv reply payload failed (rank 0 closed/timeout, errno=%d)", tag, errno);
            return false;
        }
        return true;
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
