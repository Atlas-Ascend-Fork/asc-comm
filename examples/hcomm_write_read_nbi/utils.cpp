/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "utils.h"
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

// 解析tcp://<ip>:<port>格式的endpoint，提取ip和port
bool ParseEndpoint(const std::string &endpoint, std::string &ip, uint16_t &port)
{
    const std::string prefix = "tcp://";
    if (endpoint.find(prefix) != 0) {
        return false;
    }
    std::string addr = endpoint.substr(prefix.size());
    auto colonPos = addr.rfind(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    ip = addr.substr(0, colonPos);
    int portTemp;
    size_t portStrLen;
    try {
        portTemp = std::stoi(addr.substr(colonPos + 1), &portStrLen);
    } catch (const std::invalid_argument& e) {
        // 字符串无法转为数字
        return false;
    } catch (const std::out_of_range& e) {
        // 数值超出int范围
        return false;
    }
    // 末尾有非数字字符
    if (portStrLen != addr.substr(colonPos + 1).size()) {
        return false;
    }
    // 合法性检查
    if (portTemp <= 0 || portTemp > 65535) {
        return false;
    }
    port = static_cast<uint16_t>(portTemp);
    return !ip.empty();
}

int SetSockTimeout(int fd, int sec, int usec)
{
    // struct timeval tv = {3, 0}; // 3秒
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = usec;

    // 接收超时
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    // 发送超时
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

// rank 0作为server监听，rank 1作为client连接，建立TCP通道
int32_t ConnectPeer(uint32_t rank, const std::string &ip, uint16_t port, int32_t &sock)
{
    if (rank == 0) {
        int32_t listenSock = socket(AF_INET, SOCK_STREAM, 0);
        if (listenSock < 0) {
            return -1;
        }
        int32_t opt = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        if (bind(listenSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 ||
            listen(listenSock, 1) < 0) {
            close(listenSock);
            return -1;
        }
        sock = accept(listenSock, nullptr, nullptr);
        close(listenSock);

        // 设置5s超时，0微秒
        if (SetSockTimeout(sock, 5, 0) < 0) {
            fprintf(stderr, "[ERROR] setsockopt timeout\n");
            close(sock);
            return -1;
        }

        return sock < 0 ? -1 : 0;
    } else {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return -1;
        }
        // 设置5s超时，0微秒
        if (SetSockTimeout(sock, 5, 0) < 0) {
            fprintf(stderr, "[ERROR] setsockopt timeout\n");
            close(sock);
            return -1;
        }
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        // 重试连接，等待rank 0的server就绪
        for (int32_t i = 0; i < 100; i++) {
            if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                return 0;
            }
            usleep(100000); // 100ms
        }
        close(sock);
        return -1;
    }
}

// 通过socket收发完整数据
int32_t SendAll(int32_t sock, const void *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, static_cast<const char*>(buf) + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += static_cast<size_t>(n);
    }
    return 0;
}

int32_t RecvAll(int32_t sock, void *buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(sock, static_cast<char*>(buf) + received, len - received, 0);
        fprintf(stderr, "[INFO] recvall recv %zd bytes\n", n);
        if (n <= 0) {
            int code = errno;
            fprintf(stderr, "[ERROR] recvall code=%s\n", strerror(code));
            return -1;
        }
        received += static_cast<size_t>(n);
    }
    return 0;
}

int32_t TcpBarrier(int32_t sock)
{
    int32_t flag = 1;
    if (SendAll(sock, &flag, sizeof(flag)) != 0) {
        fprintf(stderr, "[ERROR] TcpBarrier sendall failed\n");
        return -1;
    }
    int32_t recv = 0;
    if (RecvAll(sock, &recv, sizeof(recv)) != 0) {
        fprintf(stderr, "[ERROR] TcpBarrier recvall failed\n");
        return -1;
    };
    return 0;
}