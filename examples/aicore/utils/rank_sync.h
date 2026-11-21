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
 *   - allgather（allgather，等长载荷按rank序收集）；
 *   - barrier（barrier，allgather的4字节特例，尽量简单）。
 *
 * endpoint一律由外部（命令行）传入，同时支持IPv4与IPv6：
 *   - IPv4格式："tcp://ip:port"，如 "tcp://127.0.0.1:29623"
 *   - IPv6格式："tcp://[ip]:port"，如 "tcp://[::1]:29623"
 * rank 0进程listen绑定该地址，其余rank进程connect连接。单机与跨机同一套代码
 * （单机传127.0.0.1或[::1]即可），并行运行多个实例时由用户显式错开端口。
 * 数据面同步（Drain/Flush/SynchronizeStream等设备侧语义）不在此处。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H
#define ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sys/socket.h>
#include <sys/types.h>

namespace examples {

// 控制通道消息头：tag区分集合调用点（同一调用点所有rank必须使用相同tag），
// len为单个rank的载荷长度（所有rank相同）。
struct rank_sync_msg_header {
    uint32_t tag;
    uint32_t len;
};

// 集合调用点tag的分配基址。样例自定义tag请从TAG_USER_BASE开始编号，保证同一样例内互不相同。
constexpr uint32_t TAG_USER_BASE = 100U;
// 公共工具perf_result.h（gather_perf_rows_to_root）专用的终局结果交换tag。
// 各样例的样例自定义tag请继续从TAG_USER_BASE开始编号，不会与它冲突。
constexpr uint32_t TAG_PERF_RESULT = TAG_USER_BASE - 1U;

class rank_sync_context {
public:
    // rank 0进程调用：绑定endpoint并接受其余rank的连接。
    // 支持IPv4（"tcp://ip:port"）和IPv6（"tcp://[ip]:port"）格式。
    // bind失败（地址被占/不可达）时Ok()为false。
    // 命名为make_listen以避免遮蔽POSIX listen()（类内create_listen_socket需要调用）。
    static rank_sync_context make_listen(const std::string& endpoint, uint32_t nranks)
    {
        rank_sync_context ctx(0U, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (parse_endpoint(endpoint, host, port)) {
            fd = create_listen_socket(host, port);
        } else {
            std::fprintf(
                stderr, "[ERROR] invalid control endpoint \"%s\", expected \"tcp://ip:port\" or \"tcp://[ip]:port\"\n",
                endpoint.c_str());
        }
        ctx.setup_server(fd);
        return ctx;
    }

    // 客户端：连接endpoint指向的rank 0（连接失败按固定间隔重试）。
    // 支持IPv4（"tcp://ip:port"）和IPv6（"tcp://[ip]:port"）格式。
    // 命名为make_connect以避免遮蔽POSIX connect()（类内connect_with_retry需要调用）。
    static rank_sync_context make_connect(uint32_t rank, uint32_t nranks, const std::string& endpoint)
    {
        rank_sync_context ctx(rank, nranks);
        std::string host;
        uint16_t port = 0U;
        int fd = -1;
        if (parse_endpoint(endpoint, host, port)) {
            fd = connect_with_retry(host, port);
            if (fd >= 0) {
                // 连接后先报身份，rank 0据此建立 fd -> rank 映射。
                const uint32_t self = rank;
                if (send_all(fd, &self, sizeof(self))) {
                    ctx.peer_fds[0] = fd;
                    fd = -1; // 所有权已转移
                    ctx.valid = true;
                }
            }
        } else {
            std::fprintf(
                stderr, "[ERROR] invalid control endpoint \"%s\", expected \"tcp://ip:port\" or \"tcp://[ip]:port\"\n",
                endpoint.c_str());
        }
        if (fd >= 0) {
            close(fd);
        }
        return ctx;
    }

    rank_sync_context(const rank_sync_context&) = delete;
    rank_sync_context& operator=(const rank_sync_context&) = delete;
    rank_sync_context(rank_sync_context&& other) noexcept : rank_sync_context(other.rank_id, other.rank_num)
    {
        valid = other.valid;
        peer_fds = std::move(other.peer_fds);
        listen_fd = other.listen_fd;
        own_listen_fd = other.own_listen_fd;
        other.valid = false;
        other.listen_fd = -1;
        other.own_listen_fd = false;
    }

    ~rank_sync_context()
    {
        for (int fd : peer_fds) {
            if (fd >= 0) {
                close(fd);
            }
        }
        if (listen_fd >= 0 && own_listen_fd) {
            close(listen_fd);
        }
    }

    bool ok() const { return valid; }

    // 本rank的编号与通信域内rank总数。
    uint32_t rank() const { return rank_id; }
    uint32_t nranks() const { return rank_num; }

    // 每个rank提供len字节，收集为nranks*len（按rank序写入recv）。所有rank必须在
    // 同一程序位置以相同tag调用，且len一致。
    bool allgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        if (!valid || len == 0U) {
            return false;
        }
        if (rank_id == 0U) {
            return server_allgather(tag, send, len, recv);
        }
        return client_allgather(tag, send, len, recv);
    }

    // barrier：allgather的4字节特例。不同调用点用不同tag。
    bool barrier(uint32_t tag)
    {
        uint32_t dummy = 0U;
        std::vector<uint32_t> all(rank_num, 0U);
        return allgather(tag, &dummy, sizeof(dummy), all.data());
    }

private:
    explicit rank_sync_context(uint32_t rank, uint32_t nranks) : rank_id(rank), rank_num(nranks), peer_fds(nranks, -1)
    {}

    static constexpr int SOCK_TIMEOUT_SEC = 60;
    static constexpr uint32_t CONNECT_RETRY_TIMES = 300U;
    static constexpr uint32_t CONNECT_RETRY_INTERVAL_MS = 100U;

    // 解析endpoint为host和port。支持两种格式：
    //   IPv4: "tcp://host:port"
    //   IPv6: "tcp://[host]:port"（RFC 3986方括号约定）
    static bool parse_endpoint(const std::string& endpoint, std::string& host, uint16_t& port)
    {
        constexpr const char* PREFIX = "tcp://";
        const size_t prefix_len = std::strlen(PREFIX);
        if (endpoint.rfind(PREFIX, 0) != 0) {
            return false;
        }
        const std::string remainder = endpoint.substr(prefix_len);
        if (remainder.empty()) {
            return false;
        }

        std::string port_str;
        if (remainder[0] == '[') {
            // IPv6格式：[host]:port
            const size_t close_bracket = remainder.find(']');
            if (close_bracket == std::string::npos || close_bracket < 2) {
                return false;
            }
            host = remainder.substr(1, close_bracket - 1);
            // ']'之后必须是':port'
            if (close_bracket + 1 >= remainder.size() || remainder[close_bracket + 1] != ':') {
                return false;
            }
            port_str = remainder.substr(close_bracket + 2);
        } else {
            // IPv4格式：host:port
            const size_t colon = remainder.rfind(':');
            if (colon == std::string::npos || colon == 0) {
                return false;
            }
            host = remainder.substr(0, colon);
            port_str = remainder.substr(colon + 1);
        }

        if (host.empty() || port_str.empty()) {
            return false;
        }
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(port_str.c_str(), &end, 10);
        if (errno != 0 || end == port_str.c_str() || *end != '\0' || value <= 0 || value > 65535L) {
            return false;
        }
        port = static_cast<uint16_t>(value);
        return true;
    }

    static bool set_sock_timeouts(int fd)
    {
        struct timeval tv = {};
        tv.tv_sec = SOCK_TIMEOUT_SEC;
        // Headers and payloads are sent separately. Disable Nagle so delayed ACK
        // does not hold a barrier reply and skew rank launch times by tens of ms.
        const int no_delay = 1;
        return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) == 0 &&
               setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
               setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
    }

    static bool send_all(int fd, const void* data, size_t size)
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

    static bool recv_all(int fd, void* data, size_t size)
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
    // 全部失败时打印最后一个errno与排查提示（端口占用是最常见原因）。
    static int create_listen_socket(const std::string& host, uint16_t port)
    {
        const std::string port_str = std::to_string(port);
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC; // 同时尝试IPv4和IPv6
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE; // 用于bind
        struct addrinfo* result = nullptr;
        const int gai_ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        if (gai_ret != 0) {
            std::fprintf(stderr, "[ERROR] resolve %s:%u failed: %s\n", host.c_str(), port, gai_strerror(gai_ret));
            return -1;
        }
        int fd = -1;
        int last_errno = 0;
        for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                last_errno = errno;
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
            last_errno = errno;
            close(fd);
            fd = -1;
        }
        freeaddrinfo(result);
        if (fd < 0) {
            std::fprintf(
                stderr,
                "[ERROR] listen %s:%u failed: %s. Check if the port is occupied (ss -ltnp / lsof -i:%u), "
                "or use another port\n",
                host.c_str(), port, std::strerror(last_errno), port);
        }
        return fd;
    }

    // 使用getaddrinfo解析地址并连接，同时支持IPv4和IPv6。
    // rank 0可能尚未开始accept（逐rank部署时启动有先后），按固定间隔重试。
    // 重试耗尽后打印最后一次errno与排查提示（对端未启动/网络不通是最常见原因）。
    static int connect_with_retry(const std::string& host, uint16_t port)
    {
        const std::string port_str = std::to_string(port);
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        const int gai_ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        if (gai_ret != 0) {
            std::fprintf(stderr, "[ERROR] resolve %s:%u failed: %s\n", host.c_str(), port, gai_strerror(gai_ret));
            return -1;
        }
        int fd = -1;
        int last_errno = 0;
        for (uint32_t attempt = 0U; attempt < CONNECT_RETRY_TIMES; ++attempt) {
            for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
                fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (fd < 0) {
                    last_errno = errno;
                    continue;
                }
                if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
                    if (set_sock_timeouts(fd)) {
                        freeaddrinfo(result);
                        return fd;
                    }
                    close(fd);
                    freeaddrinfo(result);
                    return -1;
                }
                last_errno = errno;
                close(fd);
                fd = -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(CONNECT_RETRY_INTERVAL_MS));
        }
        freeaddrinfo(result);
        if (fd < 0) {
            std::fprintf(
                stderr,
                "[ERROR] connect %s:%u failed after %u attempts: %s. Check that rank 0 has started and "
                "the port is reachable (firewall/security group rules)\n",
                host.c_str(), port, CONNECT_RETRY_TIMES, std::strerror(last_errno));
        }
        return fd;
    }

    // listener_fd为listen()绑定好的监听socket，本上下文拥有并在析构时关闭。
    // listener_fd<0表示构造失败。依次接受其余rank的连接；连接后对方先发送自己的rank号。
    void setup_server(int listener_fd)
    {
        if (listener_fd < 0) {
            return;
        }
        listen_fd = listener_fd;
        own_listen_fd = true;
        for (uint32_t accepted = 0U; accepted + 1U < rank_num; ++accepted) {
            int fd = -1;
            do {
                fd = accept(listen_fd, nullptr, nullptr);
            } while (fd < 0 && errno == EINTR);
            if (fd < 0 || !set_sock_timeouts(fd)) {
                if (fd >= 0) {
                    close(fd);
                }
                return;
            }
            uint32_t peer_rank = 0U;
            if (!recv_all(fd, &peer_rank, sizeof(peer_rank)) || peer_rank == 0U || peer_rank >= rank_num ||
                peer_fds[peer_rank] >= 0) {
                close(fd);
                return;
            }
            peer_fds[peer_rank] = fd;
        }
        valid = true;
    }

    bool server_allgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        auto* out = static_cast<uint8_t*>(recv);
        std::memcpy(out, send, len); // slot 0：rank 0自己的载荷
        for (uint32_t peer = 1U; peer < rank_num; ++peer) {
            rank_sync_msg_header header = {};
            if (!recv_all(peer_fds[peer], &header, sizeof(header)) || header.tag != tag || header.len != len) {
                return false;
            }
            if (!recv_all(peer_fds[peer], out + static_cast<size_t>(peer) * len, len)) {
                return false;
            }
        }
        // 全员数据到齐后回发整表。
        for (uint32_t peer = 1U; peer < rank_num; ++peer) {
            const rank_sync_msg_header reply = {tag, rank_num * len};
            if (!send_all(peer_fds[peer], &reply, sizeof(reply)) ||
                !send_all(peer_fds[peer], out, static_cast<size_t>(rank_num) * len)) {
                return false;
            }
        }
        return true;
    }

    bool client_allgather(uint32_t tag, const void* send, uint32_t len, void* recv)
    {
        const rank_sync_msg_header header = {tag, len};
        if (!send_all(peer_fds[0], &header, sizeof(header)) || !send_all(peer_fds[0], send, len)) {
            return false;
        }
        rank_sync_msg_header reply = {};
        if (!recv_all(peer_fds[0], &reply, sizeof(reply)) || reply.tag != tag || reply.len != rank_num * len) {
            return false;
        }
        return recv_all(peer_fds[0], recv, static_cast<size_t>(rank_num) * len);
    }

    uint32_t rank_id;
    uint32_t rank_num;
    // peer_fds[rank]是与该rank的连接；rank 0侧slot 0未用（自己），客户端侧只有slot 0。
    std::vector<int> peer_fds;
    int listen_fd = -1;
    bool own_listen_fd = false;
    bool valid = false;
};

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_RANK_SYNC_H
