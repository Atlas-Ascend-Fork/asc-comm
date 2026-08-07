/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_base.h
 * \brief Hcomm base class
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H
#endif

#ifndef IMPL_COMM_API_AICORE_HCOMM_COMMON_HCOMM_BASE_H
#define IMPL_COMM_API_AICORE_HCOMM_COMMON_HCOMM_BASE_H

#include "hcomm_inner_def.h"

namespace AscendC {
template <CommProtocol commProtocol>
class HcommImpl {
public:
    __aicore__ inline HcommImpl(){};
    __aicore__ inline ~HcommImpl(){};
    __aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return HCOMM_FAILED;
    }

    template <typename T>
    __aicore__ inline int32_t Init(const LocalTensor<T>& buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return HCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return HCOMM_FAILED;
    }

    template <
        typename T, HcommUrmaReduceOp reduceOp, bool commit = true, pipe_t commitPipe = PIPE_S,
        pipe_t reqPipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteReduceNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
    {
        return HCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return HCOMM_FAILED;
    }

    template <
        typename T, bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value)
    {
        return HCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
    {
        return HCOMM_FAILED;
    }

    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t Commit(ChannelHandle channel)
    {
        return HCOMM_FAILED;
    }

    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Drain(ChannelHandle channel)
    {
        return HCOMM_FAILED;
    }
};
} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_HCOMM_COMMON_HCOMM_BASE_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H
#endif
