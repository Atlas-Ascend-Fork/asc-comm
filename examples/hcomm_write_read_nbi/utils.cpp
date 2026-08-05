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

// 设置 socket 地址复用，以及收发超时时间，避免异常场景下永久阻塞
static int32_t SetSockTimeout(int32_t fd, int32_t sec, int32_t usec)
{
    int32_t opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return FAIL;
    }

    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = usec;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return FAIL;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return FAIL;
    }
    return SUCCESS;
}

// 循环发送直到 len 字节全部写出，处理 send 的部分发送
int32_t SendAll(int32_t sock, const void* buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, static_cast<const char*>(buf) + sent, len - sent, 0);
        if (n <= 0) {
            return FAIL;
        }
        sent += static_cast<size_t>(n);
    }
    return SUCCESS;
}

// 循环接收直到 len 字节全部读满，处理 recv 的部分接收
int32_t RecvAll(int32_t sock, void* buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(sock, static_cast<char*>(buf) + received, len - received, 0);
        if (n <= 0) {
            int code = errno;
            fprintf(stderr, "[ERROR] recvall code=%s\n", std::strerror(code));
            return FAIL;
        }
        received += static_cast<size_t>(n);
    }
    return SUCCESS;
}

// 环形拓扑上的同步屏障：先向 prev/next 发送标记，再分别收齐两侧标记
int32_t RingBarrier(int32_t prevSocket, int32_t nextSocket, uint32_t nranks)
{
    if (nranks <= 1U) {
        return SUCCESS;
    }

    int32_t flag = 1;
    if (SendAll(prevSocket, &flag, sizeof(flag)) != SUCCESS) {
        fprintf(stderr, "[ERROR] RingBarrier send to prev failed\n");
        return FAIL;
    }
    if (SendAll(nextSocket, &flag, sizeof(flag)) != SUCCESS) {
        fprintf(stderr, "[ERROR] RingBarrier send to next failed\n");
        return FAIL;
    }

    int32_t receivedFlag = 0;
    if (RecvAll(prevSocket, &receivedFlag, sizeof(receivedFlag)) != SUCCESS) {
        fprintf(stderr, "[ERROR] RingBarrier recv from prev failed\n");
        return FAIL;
    }
    if (RecvAll(nextSocket, &receivedFlag, sizeof(receivedFlag)) != SUCCESS) {
        fprintf(stderr, "[ERROR] RingBarrier recv from next failed\n");
        return FAIL;
    }

    return SUCCESS;
}

// 计算环上的下一个 rank
static uint32_t GetNextRank(uint32_t rank, uint32_t nranks) { return (rank + 1U) % nranks; }

// 在指定 ip:port 上创建监听 socket，失败时释放已创建的 fd
static int32_t CreateListenSocket(const char* ip, uint16_t port, int32_t& listenSock)
{
    listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        fprintf(stderr, "[ERROR] socket failed\n");
        return FAIL;
    }
    if (SetSockTimeout(listenSock, 5, 0) != SUCCESS) {
        fprintf(stderr, "[ERROR] setsockopt timeout\n");
        close(listenSock);
        listenSock = -1;
        return FAIL;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (bind(listenSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 || listen(listenSock, 1) < 0) {
        fprintf(stderr, "[ERROR] bind/listen failed on port %u\n", port);
        close(listenSock);
        listenSock = -1;
        return FAIL;
    }
    return SUCCESS;
}

// 连接指定 ip:port，对端未就绪时按固定间隔重试，最多重试 retryTimes 次
static int32_t ConnectWithRetry(const char* ip, uint16_t port, int32_t& sock, uint16_t retryTimes = 100)
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[ERROR] socket failed\n");
        return FAIL;
    }
    if (SetSockTimeout(sock, 5, 0) != SUCCESS) {
        fprintf(stderr, "[ERROR] setsockopt timeout\n");
        close(sock);
        sock = -1;
        return FAIL;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    int32_t interval = 100000;
    for (uint16_t i = 0; i < retryTimes; i++) {
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            return SUCCESS;
        }
        usleep(interval);
    }
    fprintf(stderr, "[ERROR] connect to port %u failed after %u retries\n", port, retryTimes);
    close(sock);
    sock = -1;
    return FAIL;
}

// 建立 host 侧 TCP 环：本节点监听自身端口等待 prev 接入，同时主动连接 next
int32_t SetupRingTopo(uint32_t rank, uint32_t nranks, const char* ip, int32_t& prevSocket, int32_t& nextSocket)
{
    uint16_t myPort = static_cast<uint16_t>(BASE_PORT + rank);
    uint32_t nextRank = GetNextRank(rank, nranks);
    uint16_t nextPort = static_cast<uint16_t>(BASE_PORT + nextRank);

    int32_t listenSock = -1;
    if (CreateListenSocket(ip, myPort, listenSock) != SUCCESS) {
        fprintf(stderr, "[ERROR] rank %u: create listen socket failed\n", rank);
        return FAIL;
    }

    if (ConnectWithRetry(ip, nextPort, nextSocket) != SUCCESS) {
        fprintf(stderr, "[ERROR] rank %u: connect to next rank %u failed\n", rank, nextRank);
        close(listenSock);
        return FAIL;
    }

    prevSocket = accept(listenSock, nullptr, nullptr);
    close(listenSock);
    if (prevSocket < 0) {
        fprintf(stderr, "[ERROR] rank %u: accept prev socket failed\n", rank);
        close(nextSocket);
        nextSocket = -1;
        return FAIL;
    }

    if (SetSockTimeout(prevSocket, 5, 0) != SUCCESS || SetSockTimeout(nextSocket, 5, 0) != SUCCESS) {
        fprintf(stderr, "[ERROR] rank %u: setsockopt timeout failed\n", rank);
        close(prevSocket);
        close(nextSocket);
        prevSocket = -1;
        nextSocket = -1;
        return FAIL;
    }

    return SUCCESS;
}

// 同步 rootInfo：root 节点逐个接受连接并下发，其他节点连接 root 后接收
int32_t ExchangeRootInfoRing(
    uint32_t rank, uint32_t nranks, const char* ip, uint16_t rootPort, void* rootInfo, uint32_t rootInfoSize)
{
    if (rank == ROOT_SERVER_RANK) {
        int32_t listenSock = -1;
        if (CreateListenSocket(ip, rootPort, listenSock) != SUCCESS) {
            fprintf(stderr, "[ERROR] root: create listen socket failed\n");
            return FAIL;
        }
        for (uint32_t i = 1U; i < nranks; ++i) {
            int32_t peerSock = accept(listenSock, nullptr, nullptr);
            if (peerSock < 0) {
                fprintf(stderr, "[ERROR] root: accept from rank %u failed\n", i);
                close(listenSock);
                return FAIL;
            }
            if (SetSockTimeout(peerSock, 5, 0) != SUCCESS) {
                fprintf(stderr, "[ERROR] root: setsockopt timeout failed for rank %u\n", i);
                close(peerSock);
                close(listenSock);
                return FAIL;
            }
            if (SendAll(peerSock, rootInfo, rootInfoSize) != SUCCESS) {
                fprintf(stderr, "[ERROR] root: send rootInfo to rank %u failed\n", i);
                close(peerSock);
                close(listenSock);
                return FAIL;
            }
            close(peerSock);
        }
        close(listenSock);
    } else {
        int32_t rootSock = -1;
        if (ConnectWithRetry(ip, rootPort, rootSock) != SUCCESS) {
            fprintf(stderr, "[ERROR] rank %u: connect to root failed\n", rank);
            return FAIL;
        }
        if (RecvAll(rootSock, rootInfo, rootInfoSize) != SUCCESS) {
            fprintf(stderr, "[ERROR] rank %u: recv rootInfo from root failed\n", rank);
            close(rootSock);
            return FAIL;
        }
        close(rootSock);
    }
    return SUCCESS;
}
