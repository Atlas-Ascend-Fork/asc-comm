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
 * \file hcomm_simt_base.h
 * \brief Hcomm SIMT base class
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H

#include "hcomm_simt_inner_def.h"

namespace AscendC::simt {

// Primary template: every protocol without a specialization fails. COMM_PROTOCOL_UB_CTP is
// specialized in impl/hcomm_simt_urma_def.h.
template <CommProtocol commProtocol>
class HcommImpl {
public:
    __simt_callee__ inline HcommImpl() {}
    __simt_callee__ inline ~HcommImpl() {}

    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return HCOMM_FAILED;
    }

    __simt_callee__ inline int32_t InitCompletionSet(
        UbcCtpCompletionSet& set, __gm__ ChannelHandle*, uint32_t, __gm__ uint8_t*, uint64_t)
    {
        set.workspace = nullptr;
        return HCOMM_FAILED;
    }
    __simt_callee__ inline int32_t PrepareCompletion(UbcCtpCompletionSet&, uint32_t, uint32_t, uint32_t)
    {
        return HCOMM_FAILED;
    }
    __simt_callee__ inline int32_t Progress(UbcCtpCompletionSet&, uint32_t, uint32_t& processed)
    {
        processed = 0U;
        return HCOMM_FAILED;
    }
    __simt_callee__ inline int32_t WaitCompletion(UbcCtpCompletionSet&, uint32_t, uint32_t = 1000000U)
    {
        return HCOMM_FAILED;
    }
    __simt_callee__ inline int32_t Drain(UbcCtpCompletionSet&) { return HCOMM_FAILED; }

    __simt_callee__ inline UbcCtpBatchHandle MakeBatchHandle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb = 1U)
    {
        (void)channel;
        (void)buff;
        (void)buffLen;
        (void)remoteBase;
        (void)itemBb;
        return {};
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return HCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value)
    {
        (void)channel;
        (void)dst;
        (void)value;
        return HCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)batchHandle;
        (void)dst;
        (void)src;
        (void)len;
        return HCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value)
    {
        (void)batchHandle;
        (void)dst;
        (void)value;
        return HCOMM_FAILED;
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return HCOMM_FAILED;
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        (void)notifyAddr;
        (void)notifyVal;
        return HCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)batchHandle;
        (void)dst;
        (void)src;
        (void)len;
        return HCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal)
    {
        (void)batchHandle;
        (void)dst;
        (void)src;
        (void)len;
        (void)notifyAddr;
        (void)notifyVal;
        return HCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
    {
        (void)channel;
        (void)dst;
        (void)fetchAddr;
        (void)addVal;
        return HCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
    {
        (void)channel;
        (void)dst;
        (void)fetchAddr;
        (void)compareVal;
        (void)swapVal;
        return HCOMM_FAILED;
    }

    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(ChannelHandle channel)
    {
        (void)channel;
        return HCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
    {
        (void)batchHandle;
        (void)dst;
        (void)fetchAddr;
        (void)addVal;
        return HCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
    {
        (void)batchHandle;
        (void)dst;
        (void)fetchAddr;
        (void)compareVal;
        (void)swapVal;
        return HCOMM_FAILED;
    }

    __simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle)
    {
        (void)batchHandle;
        return HCOMM_FAILED;
    }

    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(UbcCtpBatchHandle& batchHandle)
    {
        (void)batchHandle;
        return HCOMM_FAILED;
    }
};

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H
#endif
