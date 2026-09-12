/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 *
 * SIMT Jetty implementation. A WRITE occupies the WQEBBs required by its
 * 48-byte header and active SGEs, up to four WQEBBs for twelve SGEs. SIMT
 * fills the SQ; publication can be performed by SIMD through PublishSq().
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_JETTY_IMPL_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_JETTY_IMPL_H

#include <cstddef>
#include <cstdint>

#include "../../common/hcomm_inner_def.h"
#include "../../common/hcomm_log.h"
#include "../../../utils/simt_utils.h"
#include <kernel_operator.h>
#include <simt_api/device_atomic_functions.h>
#include <simt_api/device_warp_functions.h>
#include <simt_api/cooperative_groups.h>

namespace AscendC::simt {
namespace jetty {

constexpr uint32_t kWqebbBytes = 64U;
constexpr uint32_t kWqebbLanes = kWqebbBytes / sizeof(uint64_t);
constexpr uint32_t kWarpSize = 32U;
constexpr uint32_t kMaxSges = 12U;
constexpr uint32_t kSlotWqebbs = 4U;
constexpr uint32_t kSqeHeaderWords = 6U;
constexpr uint32_t kDwqeDbOffset = 0x80U;
constexpr uint64_t kWriteHeader = 0x0000030030010000ULL;
constexpr uint64_t kNopHeader = 0x0000110000000000ULL;
constexpr uint64_t kFinalFlags = (1ULL << 18U) | (1ULL << 19U) | (1ULL << 21U);
constexpr uint64_t kNopControlFlags = (1ULL << 28U) | (1ULL << 29U);
constexpr uint64_t kNopFinalFlags = (1ULL << 16U) | kFinalFlags | kNopControlFlags;

enum class HcommPostOpcode : uint8_t {
    kWrite,
    kWriteValue,
    kWriteWithNotify,
};

template <uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes = 0U>
__simt_callee__ constexpr uint32_t PostSendPayloadWords()
{
    if constexpr (kOpcode == HcommPostOpcode::kWrite) {
        return kSqeHeaderWords + kSgeNum * 2U;
    } else if constexpr (kOpcode == HcommPostOpcode::kWriteWithNotify) {
        return 2U * kWqebbLanes;
    } else {
        return kSqeHeaderWords + (kInlineBytes + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    }
}

template <uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes = 0U>
__simt_callee__ constexpr uint32_t PostSendPayloadWqebbs()
{
    return (PostSendPayloadWords<kSgeNum, kOpcode, kInlineBytes>() + kWqebbLanes - 1U) / kWqebbLanes;
}

struct alignas(8) HcommPeerInfo {
    uint64_t tpIdNumSgesRemoteTokenId = 0U;
    uint64_t remoteEid[2] = {};
    uint64_t remoteTokenValueUdf = 0U;
    uint64_t remoteBaseAddr = 0U;
    uint64_t remoteBufferSize = 0U;
};
struct alignas(8) HcommJettyInfo {
    uint64_t sqBaseAddr = 0U;
    uint64_t sqHeadAddr = 0U;
    uint64_t completionTailAddr = 0U;
    uint64_t sqDoorbellAddr = 0U;
    uint64_t cqBaseAddr = 0U;
    uint64_t cqTailAddr = 0U;
    uint64_t cqDoorbellAddr = 0U;
    uint64_t packedHead = 0U;
    uint32_t numWqebbBytes = 0U;
    uint32_t numCqeBytes = 0U;
    uint32_t sqDepth = 0U;
    uint32_t cqDepth = 0U;
};
static_assert(sizeof(HcommPeerInfo) == 48U, "SIMT/SIMD peer metadata ABI mismatch");
static_assert(offsetof(HcommPeerInfo, remoteEid) == sizeof(uint64_t), "unexpected peer metadata layout");
static_assert(offsetof(HcommPeerInfo, remoteTokenValueUdf) == 3U * sizeof(uint64_t),
              "unexpected peer metadata layout");
static_assert(sizeof(HcommJettyInfo) == 80U, "SIMT/SIMD Jetty metadata ABI mismatch");
static_assert(offsetof(HcommJettyInfo, packedHead) == 56U, "SIMT/SIMD packed head offset mismatch");

struct HcommSge {
    uint64_t addr = 0U;
    int len = 0;
};
namespace detail {
template <typename T>
__simt_callee__ __forceinline__ T Load(const uint64_t addr, const uint32_t offset)
{
    return *reinterpret_cast<__gm__ T*>(addr + offset);
}
__simt_callee__ __forceinline__ uint32_t Owner(const uint32_t head, const uint32_t depth)
{
    return ((head & depth) == 0U) ? 1U : 0U;
}
__simt_callee__ __forceinline__ uint64_t PeerWord(
    __ubuf__ const HcommPeerInfo* p, const uint32_t word, const uint32_t sgeNum, __gm__ void* dst)
{
    auto* words = reinterpret_cast<__ubuf__ const uint64_t*>(p);
    const uint64_t cached = words[word - 1U];
    const uint64_t word1 = (cached & 0x000FFFFF00FFFFFFULL) |
                           (static_cast<uint64_t>(sgeNum) << 24U);
    const uint64_t word1Mask = 0ULL - static_cast<uint64_t>(word == 1U);
    const uint64_t word5Mask = 0ULL - static_cast<uint64_t>(word == 5U);
    return (cached & ~(word1Mask | word5Mask)) |
           (word1 & word1Mask) |
           (reinterpret_cast<uint64_t>(dst) & word5Mask);
}
} // namespace detail

class HcommPeer {
public:
    __simt_callee__ __forceinline__ HcommPeer() = default;
    __simt_callee__ __forceinline__ HcommPeer(
        __ubuf__ HcommPeerInfo* info, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        Init(info, jettyTable, jettyIdx);
    }
    __simt_callee__ __forceinline__ HcommPeer(__ubuf__ HcommPeerInfo* info, uint64_t channelAddr)
    {
        Attach(info, channelAddr);
    }
    __simt_callee__ __forceinline__ __ubuf__ HcommPeerInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommPeerInfo*>(infoAddr);
    }

private:
    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommPeerInfo* info, uint64_t channelAddr)
    {
        infoAddr = reinterpret_cast<uint64_t>(info);
        channel = channelAddr;
    }
    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommPeerInfo* info, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        const uint64_t table = reinterpret_cast<uint64_t>(jettyTable);
        HCOMM_DEBUG_TRAP_IF(table == 0U || info == nullptr, "invalid SIMT HcommPeer initialization\n");
        const uint64_t channelAddr = *reinterpret_cast<__gm__ uint64_t*>(table + jettyIdx * sizeof(uint64_t));
        Attach(info, channelAddr);
        HCOMM_DEBUG_TRAP_IF(channelAddr == 0U, "invalid SIMT HcommPeer channel\n");
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t rbAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, remoteBufferAddr));
        const uint32_t rbNum = detail::Load<uint32_t>(channelAddr, offsetof(ChannelEntity, remoteBufferNum));
        HCOMM_DEBUG_TRAP_IF(sqCtxAddr == 0U || rbAddr == 0U || rbNum == 0U,
                            "invalid SIMT HcommPeer channel context\n");
        auto* sq = reinterpret_cast<__gm__ SqContext*>(sqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* rb = reinterpret_cast<__gm__ RegedBufferEntity*>(rbAddr) + rbNum - 1U;
        info->tpIdNumSgesRemoteTokenId = static_cast<uint64_t>(sq->contextInfo.ubJfs.tpID) |
            (static_cast<uint64_t>(rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
        uint64_t remoteEidL = 0U;
        uint64_t remoteEidH = 0U;
        for (uint32_t i = 0U; i < 8U; ++i) {
            remoteEidL |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i]) << (i * 8U);
            remoteEidH |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i + 8U]) << (i * 8U);
        }
        info->remoteEid[0] = remoteEidL;
        info->remoteEid[1] = remoteEidH;
        info->remoteTokenValueUdf = rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        info->remoteBaseAddr = rb->bufferInfo.rma.addr;
        info->remoteBufferSize = rb->bufferInfo.rma.size;
    }

private:
    uint64_t infoAddr = 0U;
    uint64_t channel = 0U;
};

class HcommJettyImpl {
    struct BatchState {
        uint32_t wqebbCount;
        uint32_t lastOffset;
        uint32_t failed;
    };

public:
    static constexpr uint32_t kNumWQEBBLanes = kWqebbLanes;
    static constexpr uint32_t kNumWQEBBsPerSQESlot = kSlotWqebbs;
    static constexpr uint32_t kNumMaxSGEsPerWrite = kMaxSges;
    static constexpr uint32_t kNumWriteWqebbs = kSlotWqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = 2U;
    static constexpr uint32_t kNumMaxSQEBytes = kSlotWqebbs * kWqebbBytes;
    static constexpr uint32_t kNumWQEBBsPerWrite = kSlotWqebbs;
    static constexpr uint32_t kNumDwqeWords = 2U * kWqebbLanes;

    __simt_callee__ __forceinline__ HcommJettyImpl() = default;
    __simt_callee__ __forceinline__ HcommJettyImpl(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    {
        Init(info, stage, table, index);
    }
    __simt_callee__ __forceinline__ HcommJettyImpl(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
    {
        Attach(info, stage, channelAddr);
    }

private:
    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    {
        const uint64_t tableAddr = reinterpret_cast<uint64_t>(table);
        HCOMM_DEBUG_TRAP_IF(tableAddr == 0U || info == nullptr,
                            "invalid SIMT HcommJetty initialization\n");
        const uint64_t channelAddr =
            *reinterpret_cast<__gm__ uint64_t*>(tableAddr + index * sizeof(uint64_t));
        Attach(info, stage, channelAddr);
        if (stage != nullptr) {
            auto* batch = reinterpret_cast<__ubuf__ BatchState*>(stage);
            batch->wqebbCount = 0U;
            batch->lastOffset = 0U;
            batch->failed = 0U;
        }
        HCOMM_DEBUG_TRAP_IF(channelAddr == 0U, "invalid SIMT HcommJetty channel\n");
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t cqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, cqContextAddr));
        HCOMM_DEBUG_TRAP_IF(sqCtxAddr == 0U || cqCtxAddr == 0U,
                            "invalid SIMT HcommJetty channel context\n");
        auto* sq = reinterpret_cast<__gm__ SqContext*>(sqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* cq = reinterpret_cast<__gm__ CqContext*>(cqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
        info->sqBaseAddr = sq->contextInfo.ubJfs.sqVa;
        info->sqHeadAddr = sq->contextInfo.ubJfs.headAddr;
        info->completionTailAddr = sq->contextInfo.ubJfs.tailAddr;
        info->sqDoorbellAddr = sq->contextInfo.ubJfs.dbVa;
        info->cqBaseAddr = cq->contextInfo.ubJfc.scqVa;
        info->cqTailAddr = cq->contextInfo.ubJfc.tailAddr;
        info->cqDoorbellAddr = cq->contextInfo.ubJfc.dbVa;
        info->numWqebbBytes = sq->contextInfo.ubJfs.wqeSize;
        info->numCqeBytes = cq->contextInfo.ubJfc.cqeSize;
        info->sqDepth = sq->contextInfo.ubJfs.sqDepth;
        info->cqDepth = cq->contextInfo.ubJfc.cqDepth;
        HCOMM_DEBUG_TRAP_IF(info->sqHeadAddr == 0U, "invalid SIMT HcommJetty SQ head\n");
        info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
    }
    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
    {
        infoAddr_ = reinterpret_cast<uint64_t>(info);
        stageAddr_ = reinterpret_cast<uint64_t>(stage);
        channel_ = channelAddr;
    }
public:
    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommJettyInfo*>(infoAddr_);
    }
private:
    __simt_callee__ __forceinline__ __ubuf__ BatchState* Batch() const
    {
        return reinterpret_cast<__ubuf__ BatchState*>(stageAddr_);
    }

public:
    template <uint32_t kMaxSges, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges, const Coop& coop = Coop{}) const
    {
        if constexpr (HcommCoopTraits<Coop>::kWarpCapable) {
            const HcommSge laneSge = sges[0];
            return PostSend<kMaxSges, HcommPostOpcode::kWrite, kDoCommit>(
                coop, peer, dst, laneSge, nullptr, nullptr, 0U, nullptr, 0U, nullptr, 0U);
        }
        return PostSend<kMaxSges, HcommPostOpcode::kWrite, kDoCommit>(
            coop, peer, dst, HcommSge{}, sges, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }

private:
    template <uint32_t kSgeNum, HcommPostOpcode kOpcode, bool kDoCommit, uint32_t kInlineBytes = 0U,
              typename Coop>
    __simt_callee__ __forceinline__ int32_t PostSend(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& laneSge,
        const HcommSge* scalarSges, __gm__ void* src,
        const uint64_t len, __gm__ void* notifyAddr, const uint64_t notifyValue,
        const uint8_t* inlineValue, const uint32_t inlineBytes) const
    {
        static_assert(kOpcode == HcommPostOpcode::kWrite || kSgeNum == 0U,
                      "non-write posts do not carry SGEs");
        static_assert(kSgeNum <= kMaxSges, "invalid SGE count");
        static_assert(PostSendPayloadWqebbs<kSgeNum, kOpcode, kInlineBytes>() <= kSlotWqebbs,
                      "post-send payload exceeds the SQ slot");
        static_assert(kOpcode != HcommPostOpcode::kWriteValue || kInlineBytes != 0U,
                      "inline WRITE requires a compile-time value size");
        const uint32_t rank = static_cast<uint32_t>(coop.thread_rank());
        const uint32_t laneRank = rank % kWarpSize;
        if constexpr (kDoCommit) {
            if (coop.size() > static_cast<int>(kWarpSize)) {
                coop.sync();
                return -1;
            }
        }
        if (coop.size() != 1) {
            coop.sync();
        }

        constexpr uint32_t kStaticPayloadWords =
            PostSendPayloadWords<kSgeNum, kOpcode, kInlineBytes>();
        constexpr uint32_t kStaticPayloadWqebbs =
            PostSendPayloadWqebbs<kSgeNum, kOpcode, kInlineBytes>();
        // Every WRITE uses the actual payload-sized SQ range. Scalar posts still use
        // the single-producer implicit state, while cooperative posts use the batch
        // state when they defer publication.
        constexpr bool kScalarWrite = !HcommCoopTraits<Coop>::kWarpCapable &&
            kOpcode == HcommPostOpcode::kWrite;
        constexpr uint32_t reserveWqebbs = kStaticPayloadWqebbs;
        uint32_t head = 0xFFFFFFFFU;
        if (laneRank == 0U) {
            auto* info = Info();
            if constexpr (kDoCommit || kScalarWrite) {
                // Immediate posts and scalar deferred posts are single-producer paths.
                // Keep the reservation in implicit state; only cooperative deferred
                // posts need the shared batch protocol below.
                auto* sqHead = reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead);
                auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
                const uint32_t depth = info->sqDepth;
                const uint32_t oldHead = *sqHead;
                uint32_t consumed = oldHead - *sqTail;
                while ((consumed > depth || reserveWqebbs > depth - consumed) && PullCqOne()) {
                    consumed = oldHead - *sqTail;
                }
                if (consumed <= depth && reserveWqebbs <= depth - consumed) {
                    head = oldHead;
                    *sqHead = oldHead + reserveWqebbs;
                    implicitBeginHead_ = oldHead;
                    implicitWqebbs_ = reserveWqebbs;
                    implicitPayloadWqebbs_ = kStaticPayloadWqebbs;
                    implicitActive_ = true;
                }
            } else if (stageAddr_ != 0U) {
                // Deferred multi-warp mode mirrors dispatch: packedHead remains the
                // published base while each warp atomically claims a UB-local offset.
                auto* batch = Batch();
                const uint32_t offset = asc_atomic_add(&batch->wqebbCount, reserveWqebbs);
                const uint32_t base = static_cast<uint32_t>(info->packedHead);
                const uint32_t consumed = base -
                    *reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
                if (consumed <= info->sqDepth && offset <= info->sqDepth - consumed &&
                    reserveWqebbs <= info->sqDepth - consumed - offset) {
                    head = base + offset;
                    asc_atomic_max(&batch->lastOffset, offset);
                } else {
                    asc_atomic_exch(&batch->failed, 1U);
                }
            }
        }
        if (coop.size() > 1) head = asc_shfl(head, 0, kWarpSize);
        if (head == 0xFFFFFFFFU) {
            if (coop.size() > 1) coop.sync();
            return coop.size() > kWarpSize ? 0 : -1;
        }

        if constexpr (kOpcode == HcommPostOpcode::kWrite && !HcommCoopTraits<Coop>::kWarpCapable) {
            // The scalar producer owns the whole fixed-size WQE. Emit each layout
            // phase directly so SGE loads never need a runtime pair classification.
            constexpr uint32_t kPairCount = kStaticPayloadWqebbs * 4U;
            StoreWriteHeaderPair<0U, kSgeNum>(peer, dst, head);
            StoreWriteHeaderPair<1U, kSgeNum>(peer, dst, head);
            StoreWriteHeaderPair<2U, kSgeNum>(peer, dst, head);
#pragma unroll
            for (uint32_t sge = 0U; sge < kSgeNum; ++sge) {
                StoreWriteSgePair(sge, scalarSges[sge], head);
            }
#pragma unroll
            for (uint32_t pair = 3U + kSgeNum; pair < kPairCount; ++pair) {
                StoreWriteZeroPair(pair * 2U, head);
            }
            asc_threadfence();
            coop.sync();
            if constexpr (kDoCommit) Commit(coop);
            return 0;
        }

        FillSq<kSgeNum, kOpcode, kInlineBytes, kStaticPayloadWqebbs>(
            coop, peer, dst, laneSge, scalarSges, src, len, notifyAddr, notifyValue,
            inlineValue, inlineBytes, head);
        asc_threadfence();
        coop.sync();
        if constexpr (kDoCommit) Commit(coop);
        return 0;
    }

public:
    template <int64_t, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
    {
        static_assert(sizeof(T) <= kWqebbBytes - kSqeHeaderWords * sizeof(uint64_t),
                      "inline value does not fit in a URMA WQEBB");
        HcommCoopThread thread;
        return PostSend<0U, HcommPostOpcode::kWriteValue, kDoCommit, sizeof(T)>(
            thread, peer, dst, HcommSge{}, nullptr, nullptr, 0U, nullptr, 0U,
            reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    template <int64_t, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len,
        __gm__ void* notifyAddr, uint64_t notifyValue)
    {
        HcommCoopThread thread;
        return PostSend<0U, HcommPostOpcode::kWriteWithNotify, kDoCommit>(
            thread, peer, dst, HcommSge{}, nullptr, src, len, notifyAddr, notifyValue, nullptr, 0U);
    }

private:
    __simt_callee__ __forceinline__ void AdvanceBatch() const
    {
        auto* info = Info();
        auto* batch = Batch();
        const uint32_t count = batch->wqebbCount;
        if (count == 0U) return;
        if (batch->failed != 0U) {
            batch->wqebbCount = 0U;
            batch->lastOffset = 0U;
            batch->failed = 0U;
            return;
        }
        const uint32_t begin = static_cast<uint32_t>(info->packedHead);
        const uint32_t end = begin + count;
        const uint32_t last = begin + batch->lastOffset;
        const uint32_t depth = info->sqDepth;
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        const uint32_t idx = last & (depth - 1U);
        const uint64_t firstWord = *reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes);
        const uint64_t header = (firstWord & ~0xFFFFULL) | (end & 0xFFFFU) | kFinalFlags;
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes), header);
        asc_threadfence();
        const uint64_t packed = static_cast<uint64_t>(end) |
            ((static_cast<uint64_t>(static_cast<uint32_t>(info->packedHead >> 32U) + 1U)) << 32U);
        info->packedHead = packed;
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr), packed);
        batch->wqebbCount = 0U;
        batch->lastOffset = 0U;
        batch->failed = 0U;
    }

public:
    __simt_callee__ __forceinline__ void Commit() const
    {
        if (!implicitActive_ || implicitWqebbs_ == 0U) {
            if (stageAddr_ != 0U && Batch()->wqebbCount != 0U) AdvanceBatch();
            return;
        }
        auto* info = Info();
        const uint32_t end = static_cast<uint32_t>(*reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead));
        const uint32_t begin = implicitBeginHead_;
        const uint32_t submittedWqebbs = implicitWqebbs_;
        HCOMM_DEBUG_TRAP_IF(end <= begin || submittedWqebbs > kSlotWqebbs || submittedWqebbs > end - begin,
                            "invalid SIMT HcommJetty commit range\n");
        const uint32_t last = end - submittedWqebbs;
        const uint32_t payloadWqebbs = implicitPayloadWqebbs_ == 0U ? submittedWqebbs : implicitPayloadWqebbs_;
        HCOMM_DEBUG_TRAP_IF(payloadWqebbs > submittedWqebbs, "invalid SIMT HcommJetty payload range\n");
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        const uint32_t idx = last & (info->sqDepth - 1U);
        const uint64_t owner = static_cast<uint64_t>(detail::Owner(last, info->sqDepth)) << 31U;
        uint64_t header = kWriteHeader | owner | idx;
        if (payloadWqebbs == submittedWqebbs) {
            header = kWriteHeader | owner | (end & 0xFFFFU) | kFinalFlags;
        } else {
            const uint32_t nopHead = end - 1U;
            const uint32_t nopIdx = nopHead & (info->sqDepth - 1U);
            const uint64_t nopOwner = static_cast<uint64_t>(detail::Owner(nopHead, info->sqDepth)) << 31U;
            const uint64_t nop = kNopHeader | nopOwner | (end & 0xFFFFU) | kNopFinalFlags;
            asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(nopIdx) * kWqebbLanes), nop);
        }
        asc_threadfence();
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes), header);
        asc_atomic_add(reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead) + 1, 1U);
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr), info->packedHead);
        auto* dwqe = reinterpret_cast<__gm__ ulonglong2*>(info->sqDoorbellAddr - kDwqeDbOffset);
        const uint32_t depth = info->sqDepth;
        for (uint32_t i = 0U; i + 1U < kNumDwqeWords; i += 2U) {
            const uint32_t srcIdx = (idx + i / kWqebbLanes) & (depth - 1U);
            asc_stwt(&dwqe[i / 2U], make_ulonglong2(
                *reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(srcIdx) * kWqebbLanes + (i % kWqebbLanes)),
                *reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(srcIdx) * kWqebbLanes + (i % kWqebbLanes) + 1U)));
        }
        asc_threadfence();
        asc_stwt(reinterpret_cast<__gm__ uint32_t*>(info->sqDoorbellAddr), end);
        implicitActive_ = false;
        implicitWqebbs_ = 0U;
        implicitPayloadWqebbs_ = 0U;
    }
    template <typename Coop>
    __simt_callee__ __forceinline__ void Commit(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0U) Commit();
        coop.sync();
    }
    __simt_callee__ __forceinline__ void AdvanceSq() const
    {
        Commit();
    }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop) const
    {
        Commit(coop);
    }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(
        const Coop& coop, uint32_t, uint32_t) const
    {
        AdvanceSq(coop);
    }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSqNoDoorbell(const Coop& coop) const
    {
        AdvanceSq(coop);
    }
    __simt_callee__ __forceinline__ void PublishSq() const
    {
        auto* info = Info();
        HCOMM_DEBUG_TRAP_IF(info == nullptr || info->sqHeadAddr == 0U || info->sqDoorbellAddr == 0U,
                            "invalid SIMT HcommJetty publish state\n");
        info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
        *reinterpret_cast<__gm__ uint32_t*>(info->sqDoorbellAddr) = static_cast<uint32_t>(info->packedHead);
    }
    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const
    {
        auto* info = Info();
        HCOMM_DEBUG_TRAP_IF(info == nullptr || info->cqTailAddr == 0U || info->cqBaseAddr == 0U ||
                                info->cqDepth == 0U || info->numCqeBytes == 0U ||
                                info->completionTailAddr == 0U || info->cqDoorbellAddr == 0U,
                            "invalid SIMT HcommJetty CQ state\n");
        const uint32_t expected = static_cast<uint32_t>(info->packedHead >> 32U);
        auto* tail = reinterpret_cast<__gm__ uint32_t*>(info->cqTailAddr);
        auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
        uint32_t current = *tail;
        uint32_t released = *sqTail;
        while (current != expected) {
            auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
                info->cqBaseAddr + static_cast<uint64_t>(info->numCqeBytes) * (current & (info->cqDepth - 1U)));
            const uint32_t phase = (current / info->cqDepth) & 1U;
            int64_t retry = 0;
            uint32_t header = *cqe;
            while (((header >> 2U) & 1U) == phase && retry++ < kTimeout) {
                asc_threadfence();
                header = *cqe;
            }
            if (retry >= kTimeout || ((header >> 16U) & 0xFFU) != 0U || ((header >> 24U) & 0xFFU) != 0U) return -1;
            const uint32_t completed = cqe[1] & (info->sqDepth - 1U);
            released += (completed + info->sqDepth - (released & (info->sqDepth - 1U))) & (info->sqDepth - 1U);
            ++released;
            ++current;
        }
        *tail = current;
        *reinterpret_cast<__gm__ uint32_t*>(info->cqDoorbellAddr) = current & 0xFFFFFFU;
        *sqTail = released;
        return 0;
    }
    template <int64_t kTimeout, typename Coop>
    __simt_callee__ __forceinline__ int32_t Drain(const Coop& coop) const
    {
        coop.sync();
        int32_t result = 0;
        if (coop.thread_rank() == 0U) result = Drain<kTimeout>();
        result = asc_shfl(result, 0, kWarpSize);
        coop.sync();
        return result;
    }

private:
    uint64_t infoAddr_ = 0U;
    uint64_t stageAddr_ = 0U;
    uint64_t channel_ = 0U;
    mutable uint32_t implicitBeginHead_ = 0U;
    mutable uint32_t implicitWqebbs_ = 0U;
    mutable uint32_t implicitPayloadWqebbs_ = 0U;
    mutable bool implicitActive_ = false;

    // Reap one completed CQE, matching the SIMD PullCq behavior. This is used only by
    // the single-producer path, where no other producer can race the CQ tail update.
    __simt_callee__ __forceinline__ bool PullCqOne() const
    {
        auto* info = Info();
        const uint32_t expected = static_cast<uint32_t>(info->packedHead >> 32U);
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(info->cqTailAddr);
        auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
        uint32_t current = *cqTail;
        if (current == expected) return false;
        auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
            info->cqBaseAddr + static_cast<uint64_t>(info->numCqeBytes) * (current & (info->cqDepth - 1U)));
        const uint32_t phase = (current / info->cqDepth) & 1U;
        uint32_t header = *cqe;
        for (uint32_t retry = 0U; ((header >> 2U) & 1U) == phase && retry < 1024U; ++retry) {
            asc_threadfence();
            header = *cqe;
        }
        if (((header >> 2U) & 1U) == phase || ((header >> 16U) & 0xFFU) != 0U ||
            ((header >> 24U) & 0xFFU) != 0U) return false;
        uint32_t released = *sqTail;
        const uint32_t completed = cqe[1] & (info->sqDepth - 1U);
        released += (completed + info->sqDepth - (released & (info->sqDepth - 1U))) & (info->sqDepth - 1U);
        ++released;
        ++current;
        *cqTail = current;
        *reinterpret_cast<__gm__ uint32_t*>(info->cqDoorbellAddr) = current & 0xFFFFFFU;
        *sqTail = released;
        return true;
    }

    template <uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes, uint32_t kStaticPayloadWqebbs,
              typename Coop>
    __simt_callee__ __forceinline__ void FillSq(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& laneSge,
        const HcommSge* scalarSges, __gm__ void* src, const uint64_t len, __gm__ void* notifyAddr,
        const uint64_t notifyValue, const uint8_t* inlineValue, const uint32_t inlineBytes,
        const uint32_t head) const
    {
        constexpr uint32_t kStaticPayloadWords =
            PostSendPayloadWords<kSgeNum, kOpcode, kInlineBytes>();
        const uint32_t depth = Info()->sqDepth;
        constexpr uint32_t totalWords = kStaticPayloadWqebbs * kWqebbLanes;
        const uint32_t rank = static_cast<uint32_t>(coop.thread_rank());
        const uint32_t laneRank = rank % kWarpSize;
        // StoreSqePair writes a complete 128-bit pair. Keep every worker on an
        // even word boundary so no pair overlaps a neighboring worker's pair.
        const uint32_t workerCount = static_cast<uint32_t>(coop.size());
        const uint32_t stepWords = workerCount * 2U;
        const uint32_t firstWord = rank * 2U;
        constexpr bool kWarpSgePath =
            HcommCoopTraits<Coop>::kWarpCapable && kOpcode == HcommPostOpcode::kWrite;
        const uint32_t roundCount = kWarpSgePath ?
            (totalWords + kWarpSize * 2U - 1U) / (kWarpSize * 2U) :
            (totalWords + stepWords - 1U) / stepWords;
        for (uint32_t round = 0U; round < roundCount; ++round) {
            const uint32_t word = kWarpSgePath ?
                round * kWarpSize * 2U + laneRank * 2U : firstWord + round * stepWords;
            const uint32_t bbHead = head + word / kWqebbLanes;
            const uint32_t sqIdx = bbHead & (depth - 1U);
            const uint64_t owner = static_cast<uint64_t>(detail::Owner(bbHead, depth)) << 31U;
            bool isSgePair = false;
            if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                // Pair boundaries and the SGE payload boundaries are both even,
                // so one range check covers both words in the pair.
                isSgePair = word >= kSqeHeaderWords && word < kStaticPayloadWords;
            }
            int firstLen = 0;
            uint64_t secondAddr = 0U;
            if constexpr (kOpcode == HcommPostOpcode::kWrite && HcommCoopTraits<Coop>::kWarpCapable) {
                const int ordinal = isSgePair ? static_cast<int>((word - kSqeHeaderWords) / 2U) : 0;
                const uint64_t packedAddr = asc_shfl(laneSge.addr, ordinal, kWarpSize);
                const int packedLen = asc_shfl(laneSge.len, ordinal, kWarpSize);
                secondAddr = isSgePair ? packedAddr : 0U;
                firstLen = isSgePair ? packedLen : 0;
            } else if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                if (isSgePair) {
                    const uint32_t index = (word - kSqeHeaderWords) / 2U;
                    firstLen = scalarSges[index].len;
                    secondAddr = scalarSges[index].addr;
                }
            }
            const uint32_t secondWord = word + 1U;
            uint64_t first = 0U;
            uint64_t second = 0U;
            if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                BuildWritePair<kSgeNum>(peer, dst, word, sqIdx, owner, firstLen, secondAddr, first, second);
            } else if constexpr (kOpcode == HcommPostOpcode::kWriteValue) {
                first = BuildValueWord<kStaticPayloadWords, kInlineBytes>(
                    peer, dst, word, sqIdx, owner, inlineValue);
                second = BuildValueWord<kStaticPayloadWords, kInlineBytes>(
                    peer, dst, secondWord, sqIdx, owner, inlineValue);
            } else {
                first = BuildNotifyWord(peer, dst, word, sqIdx, owner, src, len, notifyAddr, notifyValue);
                second = BuildNotifyWord(peer, dst, secondWord, sqIdx, owner, src, len, notifyAddr, notifyValue);
            }
            if constexpr (HcommCoopTraits<Coop>::kWarpCapable) {
                if (laneRank < kStaticPayloadWqebbs * 4U) {
                    StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), depth, head, word, first, second);
                }
            } else {
                StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), depth, head, word, first, second);
            }
        }
    }

    template <uint32_t kSgeNum>
    __simt_callee__ __forceinline__ static void BuildWritePair(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t word, const uint32_t sqIdx,
        const uint64_t owner, const int firstLen, const uint64_t secondAddr,
        uint64_t& first, uint64_t& second)
    {
        if (word == 0U) {
            first = kWriteHeader | owner | sqIdx;
            second = detail::PeerWord(peer.Info(), 1U, kSgeNum, dst);
            return;
        }
        if (word < kSqeHeaderWords) {
            first = detail::PeerWord(peer.Info(), word, kSgeNum, dst);
            second = detail::PeerWord(peer.Info(), word + 1U, kSgeNum, dst);
            return;
        }
        first = static_cast<uint64_t>(firstLen);
        second = secondAddr;
    }

    template <uint32_t kPair, uint32_t kSgeNum>
    __simt_callee__ __forceinline__ void StoreWriteHeaderPair(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t head) const
    {
        constexpr uint32_t word = kPair * 2U;
        const uint32_t depth = Info()->sqDepth;
        const uint32_t bbHead = head + word / kWqebbLanes;
        const uint32_t sqIdx = bbHead & (depth - 1U);
        const uint64_t owner = static_cast<uint64_t>(detail::Owner(bbHead, depth)) << 31U;
        uint64_t first = 0U;
        uint64_t second = 0U;
        if constexpr (kPair == 0U) {
            first = kWriteHeader | owner | sqIdx;
            second = detail::PeerWord(peer.Info(), 1U, kSgeNum, dst);
        } else {
            first = detail::PeerWord(peer.Info(), word, kSgeNum, dst);
            second = detail::PeerWord(peer.Info(), word + 1U, kSgeNum, dst);
        }
        StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), depth, head, word, first, second);
    }

    __simt_callee__ __forceinline__ void StoreWriteSgePair(
        const uint32_t sge, const HcommSge& value, const uint32_t head) const
    {
        const uint32_t word = kSqeHeaderWords + sge * 2U;
        StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), Info()->sqDepth, head, word,
                     static_cast<uint64_t>(value.len), value.addr);
    }

    __simt_callee__ __forceinline__ void StoreWriteZeroPair(
        const uint32_t word, const uint32_t head) const
    {
        StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), Info()->sqDepth, head, word, 0U, 0U);
    }

    template <uint32_t kPayloadWords, uint32_t kInlineBytes>
    __simt_callee__ __forceinline__ static uint64_t BuildValueWord(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t word, const uint32_t sqIdx,
        const uint64_t owner, const uint8_t* value)
    {
        if (word == 0U) {
            return kWriteHeader | owner | sqIdx | (1ULL << 22U) |
                (static_cast<uint64_t>(kInlineBytes & 0x3FFU) << 54U);
        }
        if (word < kSqeHeaderWords) return detail::PeerWord(peer.Info(), word, 0U, dst);
        if (word < kPayloadWords) {
            const uint32_t offset = (word - kSqeHeaderWords) * sizeof(uint64_t);
            uint64_t result = 0U;
            for (uint32_t byte = 0U; byte < sizeof(uint64_t) && offset + byte < kInlineBytes; ++byte) {
                result |= static_cast<uint64_t>(value[offset + byte]) << (byte * 8U);
            }
            return result;
        }
        return 0U;
    }

    __simt_callee__ __forceinline__ static uint64_t BuildNotifyWord(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t word, const uint32_t sqIdx,
        const uint64_t owner, __gm__ void* src, const uint64_t len, __gm__ void* notifyAddr,
        const uint64_t notifyValue)
    {
        if (word == 0U) {
            return (kWriteHeader & ~(0xFFULL << 40U)) | (static_cast<uint64_t>(0x0CU) << 40U) | owner | sqIdx;
        }
        if (word < kSqeHeaderWords) return detail::PeerWord(peer.Info(), word, 1U, dst);
        if (word == kSqeHeaderWords) {
            return static_cast<uint64_t>((peer.Info()->tpIdNumSgesRemoteTokenId >> 32U) & 0xFFFFFU) |
                ((peer.Info()->remoteTokenValueUdf & 0xFFFFFFFFULL) << 32U);
        }
        if (word == kSqeHeaderWords + 1U) return reinterpret_cast<uint64_t>(notifyAddr);
        if (word == kSqeHeaderWords + 2U) return notifyValue;
        if (word == kSqeHeaderWords + 4U) return static_cast<uint64_t>(len);
        if (word == kSqeHeaderWords + 5U) return reinterpret_cast<uint64_t>(src);
        return 0U;
    }

    __simt_callee__ __forceinline__ static void StoreSqePair(
        __gm__ uint64_t* sq, const uint32_t depth, const uint32_t head, const uint32_t firstWord,
        const uint64_t first, const uint64_t second)
    {
        const uint32_t bbOffset = firstWord / kWqebbLanes;
        const uint32_t sqIdx = (head + bbOffset) & (depth - 1U);
        auto* pairs = reinterpret_cast<__gm__ ulonglong2*>(
            sq + static_cast<uint64_t>(sqIdx) * kWqebbLanes);
        asc_stwt(pairs + (firstWord % kWqebbLanes) / 2U, make_ulonglong2(first, second));
    }

};

} // namespace AscendC::simt::jetty
} // namespace AscendC::simt

namespace AscendC::simt {
using jetty::HcommJettyInfo;
using jetty::HcommPeer;
using jetty::HcommPeerInfo;
using jetty::HcommSge;
using jetty::HcommJettyImpl;
}

#endif
