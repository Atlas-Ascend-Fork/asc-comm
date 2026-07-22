/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "utils.h"

static int32_t SetSockTimeout(int32_t fd, int32_t sec, int32_t usec)
{
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = usec;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

int32_t ConnectPeer(uint32_t rank, const char *ip, uint16_t port, int32_t &sock)
{
    if (rank == 0U) {
        int32_t listenSock = socket(AF_INET, SOCK_STREAM, 0);
        if (listenSock < 0) {
            return -1;
        }
        int32_t opt = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        if (bind(listenSock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0
            || listen(listenSock, 1) < 0) {
            close(listenSock);
            return -1;
        }
        sock = accept(listenSock, nullptr, nullptr);
        close(listenSock);

        if (SetSockTimeout(sock, 5, 0) < 0) {
            fprintf(stderr, "[ERROR] setsockopt timeout\n");
            close(sock);
            return -1;
        }

        return sock < 0 ? -1 : 0;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    if (SetSockTimeout(sock, 5, 0) < 0) {
        fprintf(stderr, "[ERROR] setsockopt timeout\n");
        close(sock);
        return -1;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    uint16_t retryTimes = 100;
    uint16_t interval = 100000;
    for (int32_t i = 0; i < retryTimes; i++) {
        if (connect(sock, static_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            return 0;
        }
        usleep(interval);
    }
    close(sock);
    return -1;
}

int32_t SendAll(int32_t sock, const void *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, static_cast<const char *>(buf) + sent, len - sent, 0);
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
        ssize_t n = recv(sock, static_cast<char *>(buf) + received, len - received, 0);
        if (n <= 0) {
            int code = errno;
            fprintf(stderr, "[ERROR] recvall code=%s\n", std::strerror(code));
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
    int32_t receivedFlag = 0;
    if (RecvAll(sock, &receivedFlag, sizeof(receivedFlag)) != 0) {
        fprintf(stderr, "[ERROR] TcpBarrier recvall failed\n");
        return -1;
    }
    return 0;
}
