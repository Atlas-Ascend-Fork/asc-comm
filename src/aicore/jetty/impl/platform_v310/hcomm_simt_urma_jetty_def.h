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
 * \file hcomm_simt_urma_jetty_def.h
 * \brief JettyImpl class skeleton shared by the public header and the v310 implementation.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_JETTY_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_JETTY_DEF_H

#include <cstdint>

#include "../../../hcomm/impl/platform_v310/hcomm_simt_urma_def.h"
#include "../../../utils/simt_utils.h"

namespace AscendC::simt {
namespace jetty {

// Geometry of the SIMT SQ layout: one WQEBB is 64B, a warp-cooperative WRITE
// reserves a fixed four-WQEBB slot for up to twelve SGEs.
constexpr uint32_t HCOMM_SIMT_WQEBB_LANES = HCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t);
constexpr uint32_t HCOMM_SIMT_WARP_SIZE = 32U;
constexpr uint32_t HCOMM_SIMT_MAX_SGES = 12U;
constexpr uint32_t HCOMM_SIMT_SLOT_WQEBBS = 4U;

struct JettySge {
    uint64_t addr = 0U;
    int len = 0;
};

enum class HcommPostOpcode : uint8_t {
    kWrite,
    kWriteValue,
    kWriteWithNotify,
};

namespace detail {
template <bool kWithNotify = false>
struct PostSendArgs {
    __gm__ void* dst;
    const JettySge* sges;
};

template <>
struct PostSendArgs<true> {
    __gm__ void* dst;
    const JettySge* sges;
    __gm__ void* notifyAddr;
    uint64_t notifyValue;
};

struct InlinePostSendArgs {
    __gm__ void* dst;
    const uint8_t* value;
};
} // namespace detail

class HcommPeer;

class JettyImpl {
public:
    static constexpr uint32_t kNumWQEBBLanes = HCOMM_SIMT_WQEBB_LANES;

    static constexpr uint32_t kNumWQEBBsPerSQESlot = HCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t kNumMaxSGEsPerWrite = HCOMM_SIMT_MAX_SGES;

    static constexpr uint32_t kNumWriteWqebbs = HCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t kNumWriteWithNotifyWqebbs = 2U;

    static constexpr uint32_t kNumMaxSQEBytes = HCOMM_SIMT_SLOT_WQEBBS * HCOMM_URMA_WQE_BB_SIZE;

    static constexpr uint32_t kNumWQEBBsPerWrite = HCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t kNumDwqeWords = 2U * HCOMM_SIMT_WQEBB_LANES;

    __simt_callee__ __forceinline__ JettyImpl() = default;

    __simt_callee__ __forceinline__
    JettyImpl(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index);

    __simt_callee__ __forceinline__
    JettyImpl(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr);

    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const;

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop = Coop{}) const;

    template <int64_t, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value);

    template <int64_t, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyValue);

    __simt_callee__ __forceinline__ void AdvanceSq() const;

    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop) const;

    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop, uint32_t, uint32_t) const;

    __simt_callee__ __forceinline__ void AdvanceImplicitSq() const;

    __simt_callee__ __forceinline__ void PublishSq() const;

    template <typename Coop>
    __simt_callee__ __forceinline__ void PublishSq(const Coop& coop) const;

    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const;

    template <int64_t kTimeout, typename Coop>
    __simt_callee__ __forceinline__ int32_t Drain(const Coop& coop) const;

    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr);

private:
    struct BatchState {
        uint32_t wqebbCount;
        uint32_t lastOffset;
        uint32_t failed;
    };

    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index);

    __simt_callee__ __forceinline__ __ubuf__ BatchState* Batch() const;

    __simt_callee__ __forceinline__ uint32_t ReserveImplicitSq(const uint32_t reserveWqebbs) const;

    __simt_callee__ __forceinline__ uint32_t ReserveBatchSq(const uint32_t reserveWqebbs) const;

    template <
        uint32_t kSgeNum, HcommPostOpcode kOpcode, bool kDoCommit, uint32_t kInlineBytes = 0U, typename Coop,
        typename Args>
    __simt_callee__ __forceinline__ int32_t PostSend(const Coop& coop, const HcommPeer& peer, const Args& args) const;

    __simt_callee__ __forceinline__ void AdvanceBatch() const;

    uint64_t infoAddr_ = 0U;

    uint64_t stageAddr_ = 0U;

    uint64_t channel_ = 0U;

    mutable uint32_t implicitBeginHead_ = 0U;

    mutable uint32_t implicitWqebbs_ = 0U;

    mutable uint32_t implicitPayloadWqebbs_ = 0U;

    mutable bool implicitActive_ = false;

    static constexpr int32_t kPollCqSuccess = 0;

    static constexpr int32_t kPollCqUnavailable = 1;

    // Poll one CQE and advance the local SQ completion tail. The caller owns CQ tail and
    // doorbell updates so this helper can serve both Drain and the incremental reservation path.
    template <int64_t kTimeout = HCOMM_SIMT_MAX_CQ_RETRY>
    __simt_callee__ __forceinline__ int32_t PollCqEntry(const uint32_t current, uint32_t& released) const;

    // Reap one completed CQE, matching the SIMD PullCq behavior. This is used only by
    // the single-producer path, where no other producer can race the CQ tail update.
    __simt_callee__ __forceinline__ bool PullCqOne() const;

    __simt_callee__ __forceinline__ static bool IsValidSge(const JettySge& sge);

    template <uint32_t kSgeNum>
    __simt_callee__ __forceinline__ static uint32_t CountScalarSges(const JettySge* sges);

    template <
        uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes, uint32_t kWqebbCount, typename Coop,
        typename Args>
    __simt_callee__ __forceinline__ void FillSqWarpLane(
        const Coop& coop, const HcommPeer& peer, const Args& args, const uint32_t head,
        const uint32_t runtimeWqebbCount, const uint32_t scalarSgeNum) const;

    template <uint32_t kPayloadWords, uint32_t kInlineBytes>
    __simt_callee__ __forceinline__ static uint64_t BuildValueWord(
        const HcommPeer& peer, const detail::InlinePostSendArgs& args, const uint32_t word, const uint32_t sqIdx);

    __simt_callee__ __forceinline__ static uint64_t BuildNotifyWord(
        const HcommPeer& peer, const detail::PostSendArgs<true>& args, const uint32_t word, const uint32_t sqIdx);

    __simt_callee__ __forceinline__ static void StoreSqePair(
        __gm__ uint64_t* sq, const uint32_t depth, const uint32_t head, const uint32_t firstWord, const uint64_t first,
        const uint64_t second);
};

} // namespace jetty
} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_JETTY_DEF_H
