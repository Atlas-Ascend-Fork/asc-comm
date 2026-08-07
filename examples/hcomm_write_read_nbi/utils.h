/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

#define CHECK(call, expect, rank)                                                          \
    do {                                                                                   \
        auto _check_ret = (call);                                                          \
        auto _check_expect = (expect);                                                     \
        if (_check_ret != _check_expect) {                                                 \
            fprintf(                                                                       \
                stderr,                                                                    \
                "[CHECK ERROR][rank=%u] %s\n"                                              \
                "  Expect   : %lld (ret = %lld)\n"                                         \
                "  Function : %s\n"                                                        \
                "  Location : %s:%d\n",                                                    \
                static_cast<unsigned>(rank), #call, static_cast<long long>(_check_expect), \
                static_cast<long long>(_check_ret), __func__, __FILE__, __LINE__);         \
            return FAIL;                                                                   \
        }                                                                                  \
    } while (0)

int32_t SendAll(int32_t sock, const void* buf, size_t len);

int32_t RecvAll(int32_t sock, void* buf, size_t len);

int32_t RingBarrier(int32_t prevSocket, int32_t nextSocket, uint32_t nranks);

int32_t SetupRingTopo(uint32_t rank, uint32_t nranks, const char* ip, int32_t& prevSocket, int32_t& nextSocket);

int32_t ExchangeRootInfoRing(
    uint32_t rank, uint32_t nranks, const char* ip, uint16_t rootPort, void* rootInfo, uint32_t rootInfoSize);
