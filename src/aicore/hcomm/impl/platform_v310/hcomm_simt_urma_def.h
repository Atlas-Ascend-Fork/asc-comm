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
 * \file hcomm_simt_urma_def.h
 * \brief Hcomm SIMT URMA definition
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H

#include "../../common/hcomm_simt_inner_def.h"

namespace AscendC::simt {

// SIMT rings the doorbell by copying a 128B DWQE to dbVa - 0x80; the scalar register write the
// SIMD path uses is not available here.
constexpr uint32_t HCOMM_URMA_DWQE_DB_OFFSET = 0x80U;
constexpr uint32_t HCOMM_URMA_DWQE_SIZE = 128U;
constexpr uint32_t HCOMM_URMA_WQE_BB_SIZE = 64U;
constexpr uint32_t HCOMM_URMA_WQE_BB_CNT = 1U;
constexpr uint32_t HCOMM_URMA_DWQE_BB_CNT = HCOMM_URMA_DWQE_SIZE / HCOMM_URMA_WQE_BB_SIZE;
constexpr uint32_t HCOMM_SIMT_MAX_CQ_RETRY = 1000000U;

// The SQE fields one post needs, read from the channel and SQ contexts by ResolvePost.
struct HcommSimtPostMeta {
    uint32_t sqDepth = 0U;
    uint32_t tpId = 0U;
    uint64_t remoteEidL = 0U;
    uint64_t remoteEidH = 0U;
    uint32_t remoteTokenId = 0U;
    uint32_t remoteTokenValue = 0U;
};

struct HcommSimtResolvedPost {
    HcommSimtPostMeta meta;
    __gm__ uint64_t* headAddr;
    // The SQ consumer index PollCq advances. The reservation reads it to tell how many basic
    // blocks the NIC has already released.
    __gm__ uint32_t* sqTailAddr;
    __gm__ uint8_t* dwqeAddr;
    uint64_t sqBaseAddr;
    uint32_t sqWqeSize;
};

// Stable fields shared by fixed-2BB Notify/Atomic WQEs for one resolved target.
struct HcommSimtPackedSqeTemplate {
    uint64_t word1 = 0U;
    uint64_t remoteEidL = 0U;
    uint64_t remoteEidH = 0U;
    uint64_t tokenWord = 0U;
    uint32_t sqDepth = 0U;
};

template <>
class HcommImpl<COMM_PROTOCOL_UBC_CTP> {
public:
    __simt_callee__ inline HcommImpl();
    __simt_callee__ inline ~HcommImpl();
    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal);
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal);
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(ChannelHandle channel);

private:
    bool initialized_ = false;

    // Reads the channel entity and SQ context from global memory on every post. Nothing is
    // cached across calls: a channel may change from one post to the next, so a cache would
    // rarely hit while still costing a validity check and an invalidation on each post.
    __simt_callee__ inline int32_t ResolvePost(
        ChannelHandle channel, __gm__ uint8_t* remoteAddr, uint64_t len, HcommSimtResolvedPost& post);

    // Claims SQ space for one submission, draining completed CQEs to release capacity when the
    // queue is full. commit selects how much free space the claim requires: a deferred post also
    // reserves room for the immediate DWQE that will publish it.
    template <bool commit>
    __simt_callee__ inline bool ReservePost(
        ChannelHandle channel, const HcommSimtResolvedPost& post, uint32_t bbCnt, uint64_t& headVal);

    // The one posting path shared by every operator. Desc describes what distinguishes them:
    // the SQ footprint (bbCnt/sgeNum) and how the WQE words are laid out. See the descriptors
    // in hcomm_simt_urma.h.
    template <typename Desc>
    __simt_callee__ inline int32_t PostWqe(ChannelHandle channel, const Desc& desc);
    __simt_callee__ inline uint32_t PollCq(ChannelHandle channel, uint32_t expectIdx);
};

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif
