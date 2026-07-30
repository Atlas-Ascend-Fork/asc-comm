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
#include "hcomm_rw_def.h"

static int32_t SetSockTimeout(int32_t fd, int32_t sec, int32_t usec)
{
    int32_t opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return -1;
    }

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

int32_t SendAll(int32_t sock, const void* buf, size_t len)
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

int32_t RecvAll(int32_t sock, void* buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(sock, static_cast<char*>(buf) + received, len - received, 0);
        if (n <= 0) {
            int code = errno;
            fprintf(stderr, "[ERROR] recvall code=%s\n", std::strerror(code));
            return -1;
        }
        received += static_cast<size_t>(n);
    }
    return 0;
}

int32_t RingBarrier(int32_t prevSocket, int32_t nextSocket, uint32_t nranks)
{
    if (nranks <= 1U) {
        return 0;
    }

    int32_t flag = 1;
    if (SendAll(prevSocket, &flag, sizeof(flag)) != 0) {
        fprintf(stderr, "[ERROR] RingBarrier send to prev failed\n");
        return -1;
    }
    if (SendAll(nextSocket, &flag, sizeof(flag)) != 0) {
        fprintf(stderr, "[ERROR] RingBarrier send to next failed\n");
        return -1;
    }

    int32_t receivedFlag = 0;
    if (RecvAll(prevSocket, &receivedFlag, sizeof(receivedFlag)) != 0) {
        fprintf(stderr, "[ERROR] RingBarrier recv from prev failed\n");
        return -1;
    }
    if (RecvAll(nextSocket, &receivedFlag, sizeof(receivedFlag)) != 0) {
        fprintf(stderr, "[ERROR] RingBarrier recv from next failed\n");
        return -1;
    }

    return 0;
}

static uint32_t GetNextRank(uint32_t rank, uint32_t nranks) { return (rank + 1U) % nranks; }

static int32_t CreateListenSocket(const char* ip, uint16_t port, int32_t& listenSock)
{
    listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        fprintf(stderr, "[ERROR] socket failed\n");
        return -1;
    }
    if (SetSockTimeout(listenSock, 5, 0) < 0) {
        fprintf(stderr, "[ERROR] setsockopt timeout\n");
        close(listenSock);
        listenSock = -1;
        return -1;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (bind(listenSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 || listen(listenSock, 1) < 0) {
        fprintf(stderr, "[ERROR] bind/listen failed on port %u\n", port);
        close(listenSock);
        listenSock = -1;
        return -1;
    }
    return 0;
}

static int32_t ConnectWithRetry(const char* ip, uint16_t port, int32_t& sock, uint16_t retryTimes = 100)
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[ERROR] socket failed\n");
        return -1;
    }
    if (SetSockTimeout(sock, 5, 0) < 0) {
        fprintf(stderr, "[ERROR] setsockopt timeout\n");
        close(sock);
        sock = -1;
        return -1;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    int32_t interval = 100000;
    for (uint16_t i = 0; i < retryTimes; i++) {
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            return 0;
        }
        usleep(interval);
    }
    fprintf(stderr, "[ERROR] connect to port %u failed after %u retries\n", port, retryTimes);
    close(sock);
    sock = -1;
    return -1;
}

int32_t SetupRingTopo(uint32_t rank, uint32_t nranks, const char* ip, int32_t& prevSocket, int32_t& nextSocket)
{
    uint16_t myPort = static_cast<uint16_t>(BASE_PORT + rank);
    uint32_t nextRank = GetNextRank(rank, nranks);
    uint16_t nextPort = static_cast<uint16_t>(BASE_PORT + nextRank);

    int32_t listenSock = -1;
    if (CreateListenSocket(ip, myPort, listenSock) != 0) {
        fprintf(stderr, "[ERROR] rank %u: create listen socket failed\n", rank);
        return -1;
    }

    if (ConnectWithRetry(ip, nextPort, nextSocket) != 0) {
        fprintf(stderr, "[ERROR] rank %u: connect to next rank %u failed\n", rank, nextRank);
        close(listenSock);
        return -1;
    }

    prevSocket = accept(listenSock, nullptr, nullptr);
    close(listenSock);
    if (prevSocket < 0) {
        fprintf(stderr, "[ERROR] rank %u: accept prev socket failed\n", rank);
        close(nextSocket);
        nextSocket = -1;
        return -1;
    }

    if (SetSockTimeout(prevSocket, 5, 0) < 0 || SetSockTimeout(nextSocket, 5, 0) < 0) {
        fprintf(stderr, "[ERROR] rank %u: setsockopt timeout failed\n", rank);
        close(prevSocket);
        close(nextSocket);
        prevSocket = -1;
        nextSocket = -1;
        return -1;
    }

    return 0;
}

int32_t ExchangeRootInfoRing(
    uint32_t rank, uint32_t nranks, const char* ip, uint16_t rootPort, void* rootInfo, uint32_t rootInfoSize)
{
    if (rank == ROOT_SERVER_RANK) {
        int32_t listenSock = -1;
        if (CreateListenSocket(ip, rootPort, listenSock) != 0) {
            fprintf(stderr, "[ERROR] root: create listen socket failed\n");
            return -1;
        }
        for (uint32_t i = 1U; i < nranks; ++i) {
            int32_t peerSock = accept(listenSock, nullptr, nullptr);
            if (peerSock < 0) {
                fprintf(stderr, "[ERROR] root: accept from rank %u failed\n", i);
                close(listenSock);
                return -1;
            }
            if (SetSockTimeout(peerSock, 5, 0) < 0) {
                fprintf(stderr, "[ERROR] root: setsockopt timeout failed for rank %u\n", i);
                close(peerSock);
                close(listenSock);
                return -1;
            }
            if (SendAll(peerSock, rootInfo, rootInfoSize) != 0) {
                fprintf(stderr, "[ERROR] root: send rootInfo to rank %u failed\n", i);
                close(peerSock);
                close(listenSock);
                return -1;
            }
            close(peerSock);
        }
        close(listenSock);
    } else {
        int32_t rootSock = -1;
        if (ConnectWithRetry(ip, rootPort, rootSock) != 0) {
            fprintf(stderr, "[ERROR] rank %u: connect to root failed\n", rank);
            return -1;
        }
        if (RecvAll(rootSock, rootInfo, rootInfoSize) != 0) {
            fprintf(stderr, "[ERROR] rank %u: recv rootInfo from root failed\n", rank);
            close(rootSock);
            return -1;
        }
        close(rootSock);
    }
    return 0;
}
