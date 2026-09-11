/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 *
 * SIMT Jetty implementation.  A Jetty reserves fixed four-BB SQ slots, which
 * is the largest URMA WRITE WQE accepted by the dispatch path (48-byte header
 * plus at most twelve 16-byte SGEs).  SIMT fills the SQ; publication can be
 * performed by SIMD through PublishSq().
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_JETTY_IMPL_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_JETTY_IMPL_H

#include <cstddef>
#include <cstdint>

#include "../../common/hcomm_inner_def.h"
#include <kernel_operator.h>
#include <simt_api/device_atomic_functions.h>
#include <simt_api/device_warp_functions.h>
#include <simt_api/cooperative_groups.h>

namespace AscendC::simt {

struct HcommCoopThread {
    __simt_callee__ __forceinline__ void sync() const {}
    __simt_callee__ __forceinline__ int size() const { return 1; }
    __simt_callee__ __forceinline__ int thread_rank() const { return 0; }
};
struct HcommCoopWarp {
    cooperative_groups::thread_block_tile<32, cooperative_groups::thread_block> group_;
    __simt_callee__ __forceinline__ HcommCoopWarp()
        : group_(cooperative_groups::tiled_partition<32>(cooperative_groups::this_thread_block())) {}
    __simt_callee__ __forceinline__ void sync() const { group_.sync(); }
    __simt_callee__ __forceinline__ int size() const { return 32; }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group_.thread_rank()); }
};
struct HcommCoopGroup {
    cooperative_groups::thread_block group_;
    __simt_callee__ __forceinline__ HcommCoopGroup() : group_(cooperative_groups::this_thread_block()) {}
    __simt_callee__ __forceinline__ void sync() const { group_.sync(); }
    __simt_callee__ __forceinline__ int size() const { return static_cast<int>(group_.size()); }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group_.thread_rank()); }
};
template <typename Coop>
struct HcommCoopTraits {
    static constexpr bool kWarpCapable = false;
    static constexpr bool kIsWarp = false;
};
template <>
struct HcommCoopTraits<HcommCoopWarp> {
    static constexpr bool kWarpCapable = true;
    static constexpr bool kIsWarp = true;
};
template <>
struct HcommCoopTraits<HcommCoopGroup> {
    static constexpr bool kWarpCapable = true;
    static constexpr bool kIsWarp = false;
};

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
    if (word == 1U) {
        return (p->tpIdNumSgesRemoteTokenId & 0xFFFFFFULL) |
               (static_cast<uint64_t>(sgeNum) << 24U) |
               (p->tpIdNumSgesRemoteTokenId & 0x000FFFFF00000000ULL);
    }
    if (word == 2U) return p->remoteEid[0];
    if (word == 3U) return p->remoteEid[1];
    if (word == 4U) return p->remoteTokenValueUdf;
    return reinterpret_cast<uint64_t>(dst);
}
__simt_callee__ __forceinline__ bool Valid(const HcommSge& sge)
{
    return sge.addr != 0U && sge.len > 0;
}
__simt_callee__ __forceinline__ int NthSet(const uint32_t mask, const uint32_t ordinal)
{
    uint32_t seen = 0U;
    for (uint32_t lane = 0U; lane < kWarpSize; ++lane) {
        if (((mask >> lane) & 1U) != 0U) {
            if (seen++ == ordinal) return static_cast<int>(lane);
        }
    }
    return -1;
}
__simt_callee__ __forceinline__ void PackWarpSge(
    const HcommSge& laneSge, const uint32_t mask, const int ordinal, uint64_t& addr, int& len)
{
    const int source = ordinal >= 0 ? NthSet(mask, static_cast<uint32_t>(ordinal)) : 0;
    const uint64_t packedAddr = asc_shfl(laneSge.addr, source, kWarpSize);
    const int packedLen = asc_shfl(laneSge.len, source, kWarpSize);
    addr = ordinal >= 0 ? packedAddr : 0U;
    len = ordinal >= 0 ? packedLen : 0;
}
} // namespace detail

struct HcommPeer {
    uint64_t infoAddr = 0U;
    uint64_t channel = 0U;

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
    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommPeerInfo* info, uint64_t channelAddr)
    {
        infoAddr = reinterpret_cast<uint64_t>(info);
        channel = channelAddr;
    }
    __simt_callee__ __forceinline__ __ubuf__ HcommPeerInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommPeerInfo*>(infoAddr);
    }
    __simt_callee__ __forceinline__ __ubuf__ HcommPeerInfo* GetInfo() const { return Info(); }
    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommPeerInfo* info, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        const uint64_t table = reinterpret_cast<uint64_t>(jettyTable);
        const uint64_t channelAddr = *reinterpret_cast<__gm__ uint64_t*>(table + jettyIdx * sizeof(uint64_t));
        Attach(info, channelAddr);
        if (channelAddr == 0U || info == nullptr) return;
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t rbAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, remoteBufferAddr));
        const uint32_t rbNum = detail::Load<uint32_t>(channelAddr, offsetof(ChannelEntity, remoteBufferNum));
        if (sqCtxAddr == 0U || rbAddr == 0U || rbNum == 0U) return;
        auto* sq = reinterpret_cast<__gm__ SqContext*>(sqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* rb = reinterpret_cast<__gm__ RegedBufferEntity*>(rbAddr) + rbNum - 1U;
        info->tpIdNumSgesRemoteTokenId = static_cast<uint64_t>(sq->contextInfo.ubJfs.tpID) |
            (static_cast<uint64_t>(rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
        info->remoteEid[0] = 0U;
        info->remoteEid[1] = 0U;
        for (uint32_t i = 0U; i < 8U; ++i) {
            info->remoteEid[0] |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i]) << (i * 8U);
            info->remoteEid[1] |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i + 8U]) << (i * 8U);
        }
        info->remoteTokenValueUdf = rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        info->remoteBaseAddr = rb->bufferInfo.rma.addr;
        info->remoteBufferSize = rb->bufferInfo.rma.size;
    }
};

class HcommJetty {
public:
    static constexpr uint32_t kNumWQEBBLanes = kWqebbLanes;
    static constexpr uint32_t kNumWQEBBsPerSQESlot = kSlotWqebbs;
    static constexpr uint32_t kNumMaxSGEsPerWrite = kMaxSges;
    static constexpr uint32_t kNumWriteWqebbs = kSlotWqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = 2U;
    static constexpr uint32_t kNumMaxSQEBytes = kSlotWqebbs * kWqebbBytes;
    static constexpr uint32_t kNumWQEBBsPerWrite = kSlotWqebbs;
    static constexpr uint32_t kNumDwqeWords = 2U * kWqebbLanes;

    __simt_callee__ __forceinline__ HcommJetty() = default;
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    {
        Init(info, stage, table, index);
    }
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
    {
        Attach(info, stage, channelAddr);
    }

    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    {
        const uint64_t tableAddr = reinterpret_cast<uint64_t>(table);
        const uint64_t channelAddr =
            tableAddr == 0U ? 0U : *reinterpret_cast<__gm__ uint64_t*>(tableAddr + index * sizeof(uint64_t));
        Attach(info, stage, channelAddr);
        if (channelAddr == 0U || info == nullptr) return;
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t cqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, cqContextAddr));
        if (sqCtxAddr == 0U || cqCtxAddr == 0U) return;
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
        if (info->sqHeadAddr != 0U) {
            info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
        }
    }
    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
    {
        infoAddr_ = reinterpret_cast<uint64_t>(info);
        stageAddr_ = reinterpret_cast<uint64_t>(stage);
        channel_ = channelAddr;
    }
    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommJettyInfo*>(infoAddr_);
    }
    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* GetInfo() const { return Info(); }

    template <uint32_t kSlots = 1U>
    __simt_callee__ __forceinline__ int32_t Reserve() const
    {
        static_assert(kSlots > 0U && kSlots <= kSlotWqebbs, "invalid SQ reservation");
        auto* info = Info();
        if (info == nullptr || info->sqDepth == 0U || info->sqHeadAddr == 0U) return -1;
        auto* head = reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead);
        auto* tail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
        for (uint32_t retry = 0U; retry < 1024U; ++retry) {
            const uint32_t oldHead = *head;
            const uint32_t consumed = tail == nullptr ? 0U : (oldHead - *tail);
            if (consumed > info->sqDepth || kSlots > info->sqDepth - consumed) return -1;
            if (asc_atomic_cas(head, oldHead, oldHead + kSlots) == oldHead) {
                if (!implicitActive_) {
                    implicitBeginHead_ = oldHead;
                    implicitReservedWqebbs_ = 0U;
                    implicitPayloadWqebbs_ = 0U;
                    implicitActive_ = true;
                }
                implicitReservedWqebbs_ += kSlots;
                return 0;
            }
        }
        return -1;
    }

    template <uint32_t kMaxSges, bool kDoCommit = true, typename Coop>
    __simt_callee__ __forceinline__ int32_t Write(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& laneSge) const
    {
        if constexpr (kDoCommit && kMaxSges == 2U) {
            return WriteTwoBb<kMaxSges>(coop, peer, dst, laneSge);
        }
        const uint32_t rank = static_cast<uint32_t>(coop.thread_rank());
        const uint32_t laneRank = rank % kWarpSize;
        const bool active = laneRank < kMaxSges;
        uint32_t mask = asc_ballot(active && detail::Valid(laneSge));
        const uint32_t count = static_cast<uint32_t>(__builtin_popcount(mask));
        if (coop.size() == 1) {
            if (laneRank != 0U || count == 0U) return -1;
            const int32_t result = WriteScalar<kMaxSges>(peer, dst, laneSge);
            if (result == 0 && kDoCommit) Commit();
            return result;
        }
        const bool hasValidSge = count != 0U;
        coop.sync();
        if (!hasValidSge) {
            coop.sync();
            // A thread-block group may contain inactive warps. They still report success so
            // every lane reaches the group-wide Commit synchronization.
            return 0;
        }
        uint32_t head = 0xFFFFFFFFU;
        if (laneRank == 0U) {
            if (Reserve<kSlotWqebbs>() == 0) head = implicitBeginHead_;
        }
        head = asc_shfl(head, 0, kWarpSize);
        if (head == 0xFFFFFFFFU) {
            coop.sync();
            // Keep a block-wide group aligned with Commit even when its writer warp
            // cannot reserve an SQ slot.
            return coop.size() > kWarpSize ? 0 : -1;
        }
        const uint32_t depth = Info()->sqDepth;
        const uint32_t payloadWords = kSqeHeaderWords + count * 2U;
        implicitPayloadWqebbs_ = (payloadWords + kWqebbLanes - 1U) / kWqebbLanes;
        const uint32_t lane = static_cast<uint32_t>(laneid());
        for (uint32_t wordIndex = lane; wordIndex < kSlotWqebbs * kWqebbLanes; wordIndex += kWarpSize) {
            const uint32_t word = wordIndex;
            const uint32_t bbHead = head + word / kWqebbLanes;
            const uint32_t sqIdx = bbHead & (depth - 1U);
            const uint64_t owner = static_cast<uint64_t>(detail::Owner(bbHead, depth)) << 31U;
            const bool isSgeWord = word >= kSqeHeaderWords && word < kSqeHeaderWords + count * 2U;
            const int sourceOrdinal = isSgeWord ? static_cast<int>((word - kSqeHeaderWords) / 2U) : -1;
            uint64_t packedSgeAddr = 0U;
            int packedSgeLen = 0;
            detail::PackWarpSge(laneSge, mask, sourceOrdinal, packedSgeAddr, packedSgeLen);
            uint64_t value = 0U;
            if (word == 0U) value = kWriteHeader | owner | sqIdx;
            else if (word < kSqeHeaderWords) value = detail::PeerWord(peer.Info(), word, count, dst);
            else if (isSgeWord) {
                const bool isLen = ((word - kSqeHeaderWords) & 1U) == 0U;
                value = isLen ? static_cast<uint64_t>(packedSgeLen) : packedSgeAddr;
            } else if ((word % kWqebbLanes) == 0U) {
                const uint32_t nopHead = head + word / kWqebbLanes;
                value = kNopHeader | (static_cast<uint64_t>(detail::Owner(nopHead, depth)) << 31U) |
                    (nopHead & 0xFFFFU);
            }
            const uint32_t pairWord = word & ~1U;
            const uint64_t second = asc_shfl(value, static_cast<int>(pairWord + 1U), kWarpSize);
            if ((word & 1U) == 0U) {
                auto* dstWords = reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr) +
                    static_cast<uint64_t>(sqIdx) * kWqebbLanes + (word % kWqebbLanes);
                asc_stwt(reinterpret_cast<__gm__ ulonglong2*>(dstWords), make_ulonglong2(value, second));
            }
        }
        asc_threadfence();
        coop.sync();
        if constexpr (kDoCommit) Commit(coop);
        return 0;
    }

    template <uint32_t kMaxSges, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kMaxSges]) const
    {
        const int32_t result = WriteScalarArray<kMaxSges>(peer, dst, sges);
        if (result == 0 && kDoCommit) Commit();
        return result;
    }

private:
    template <uint32_t kMaxSges, typename Coop>
    __simt_callee__ __forceinline__ int32_t WriteTwoBb(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& laneSge) const
    {
        static_assert(kMaxSges == 2U, "two-BB path requires two SGE lanes");
        const uint32_t laneRank = static_cast<uint32_t>(coop.thread_rank());
        const uint32_t mask = asc_ballot(detail::Valid(laneSge)) & ((1U << kMaxSges) - 1U);
        const uint32_t count = static_cast<uint32_t>(__builtin_popcount(mask));
        if (count == 0U) {
            coop.sync();
            return -1;
        }
        coop.sync();
        uint32_t head = 0xFFFFFFFFU;
        if (laneRank == 0U && Reserve<2U>() == 0) {
            head = implicitBeginHead_;
            implicitPayloadWqebbs_ = count == 1U ? 1U : 2U;
        }
        head = asc_shfl(head, 0, kWarpSize);
        if (head == 0xFFFFFFFFU) {
            coop.sync();
            return -1;
        }
        const uint32_t depth = Info()->sqDepth;
        uint64_t value = 0U;
        const uint32_t word = static_cast<uint32_t>(laneid());
        const uint32_t bb = head + word / kWqebbLanes;
        const uint32_t idx = bb & (depth - 1U);
        const uint64_t owner = static_cast<uint64_t>(detail::Owner(bb, depth)) << 31U;
        const bool isSgeWord = word >= kSqeHeaderWords && word < kSqeHeaderWords + count * 2U;
        const int ordinal = isSgeWord ? static_cast<int>((word - kSqeHeaderWords) / 2U) : -1;
        uint64_t packedSgeAddr = 0U;
        int packedSgeLen = 0;
        detail::PackWarpSge(laneSge, mask, ordinal, packedSgeAddr, packedSgeLen);
        if (word == 0U) {
            value = kWriteHeader | owner | idx;
        } else if (word < kSqeHeaderWords) {
            value = detail::PeerWord(peer.Info(), word, count, dst);
        } else if (isSgeWord) {
            value = ((word - kSqeHeaderWords) & 1U) == 0U ?
                static_cast<uint64_t>(packedSgeLen) : packedSgeAddr;
        } else if (word == kWqebbLanes) {
            value = kNopHeader | owner | idx;
        }
        const uint32_t pairWord = word & ~1U;
        uint64_t second = asc_shfl(value, static_cast<int>(pairWord + 1U), kWarpSize);
        if (word < kNumDwqeWords && (word & 1U) == 0U) {
            auto* sq = reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr);
            asc_stwt(reinterpret_cast<__gm__ ulonglong2*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes +
                                                          (word % kWqebbLanes)),
                     make_ulonglong2(value, second));
        }
        coop.sync();
        Commit(coop);
        coop.sync();

        // Commit has installed the final header in SQ. Recreate those two words in the
        // DWQE before exposing its doorbell window.
        if (word == 0U) {
            const uint32_t end = head + 2U;
            if (count == 2U) value = kWriteHeader | owner | (end & 0xFFFFU) | kFinalFlags;
            else value = kWriteHeader | owner | (head & 0xFFFFU);
        } else if (count == 1U && word == kWqebbLanes) {
            const uint32_t nopHead = head + 1U;
            const uint64_t nopOwner = static_cast<uint64_t>(detail::Owner(nopHead, depth)) << 31U;
            value = kNopHeader | nopOwner | ((head + 2U) & 0xFFFFU) | kNopFinalFlags;
        }
        second = asc_shfl(value, static_cast<int>(pairWord + 1U), kWarpSize);
        if (word < kNumDwqeWords && (word & 1U) == 0U) {
            auto* dwqe = reinterpret_cast<__gm__ ulonglong2*>(Info()->sqDoorbellAddr - kDwqeDbOffset);
            asc_stwt(&dwqe[word / 2U], make_ulonglong2(value, second));
        }
        asc_threadfence();
        coop.sync();
        return 0;
    }

public:
    template <int64_t, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        HcommSge sge{reinterpret_cast<uint64_t>(src), static_cast<int>(len)};
        const int32_t result = WriteScalar<1U>(peer, dst, sge);
        if (result == 0 && kDoCommit) Commit();
        return result;
    }

    template <int64_t, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
    {
        static_assert(sizeof(T) <= kWqebbBytes - kSqeHeaderWords * sizeof(uint64_t),
                      "inline value does not fit in a URMA WQEBB");
        if (Reserve<kSlotWqebbs>() != 0) return -1;
        implicitPayloadWqebbs_ = (kSqeHeaderWords + (sizeof(T) + 7U) / 8U + kWqebbLanes - 1U) / kWqebbLanes;
        auto* info = Info();
        const uint32_t head = implicitBeginHead_;
        const uint32_t depth = info->sqDepth;
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        for (uint32_t word = 0U; word < kSlotWqebbs * kWqebbLanes; word += 2U) {
            uint64_t values[2] = {};
            for (uint32_t j = 0U; j < 2U; ++j) {
                const uint32_t w = word + j;
                const uint32_t bb = head + w / kWqebbLanes;
                const uint32_t idx = bb & (depth - 1U);
                if (w == 0U) values[j] = kWriteHeader | (static_cast<uint64_t>(detail::Owner(bb, depth)) << 31U) |
                    idx | (1ULL << 22U) | (static_cast<uint64_t>(sizeof(T) & 0x3FFU) << 54U);
                else if (w < kSqeHeaderWords) values[j] = detail::PeerWord(peer.Info(), w, 0U, dst);
                else if (w < kSqeHeaderWords + (sizeof(T) + 7U) / 8U) {
                    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
                    const uint32_t offset = (w - kSqeHeaderWords) * 8U;
                    for (uint32_t b = 0U; b < 8U && offset + b < sizeof(T); ++b)
                        values[j] |= static_cast<uint64_t>(bytes[offset + b]) << (8U * b);
                } else if (w % kWqebbLanes == 0U) values[j] = kNopHeader |
                    (static_cast<uint64_t>(detail::Owner(bb, depth)) << 31U) | idx;
            }
            const uint32_t bb = head + word / kWqebbLanes;
            const uint32_t idx = bb & (depth - 1U);
            asc_stwt(reinterpret_cast<__gm__ ulonglong2*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes + word % kWqebbLanes),
                     make_ulonglong2(values[0], values[1]));
        }
        asc_threadfence();
        if (kDoCommit) Commit();
        return 0;
    }

    template <int64_t, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len,
        __gm__ void* notifyAddr, uint64_t notifyValue)
    {
        if (Reserve<2U>() != 0) return -1;
        implicitPayloadWqebbs_ = 2U;
        auto* info = Info();
        const uint32_t head = implicitBeginHead_;
        const uint32_t depth = info->sqDepth;
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        for (uint32_t word = 0U; word < 2U * kWqebbLanes; word += 2U) {
            uint64_t values[2] = {};
            for (uint32_t j = 0U; j < 2U; ++j) {
                const uint32_t w = word + j;
                const uint32_t bb = head + w / kWqebbLanes;
                const uint32_t idx = bb & (depth - 1U);
                if (w == 0U) values[j] = (kWriteHeader & ~(0xFFULL << 40U)) |
                    (static_cast<uint64_t>(0x0CU) << 40U) |
                    (static_cast<uint64_t>(detail::Owner(bb, depth)) << 31U) | idx;
                else if (w < kSqeHeaderWords) values[j] = detail::PeerWord(peer.Info(), w, 1U, dst);
                else if (w == kSqeHeaderWords) values[j] =
                    static_cast<uint64_t>((peer.Info()->tpIdNumSgesRemoteTokenId >> 32U) & 0xFFFFFU) |
                    ((peer.Info()->remoteTokenValueUdf & 0xFFFFFFFFULL) << 32U);
                else if (w == kSqeHeaderWords + 1U) values[j] = reinterpret_cast<uint64_t>(notifyAddr);
                else if (w == kSqeHeaderWords + 2U) values[j] = notifyValue;
                else if (w == kSqeHeaderWords + 4U) values[j] = static_cast<uint64_t>(len);
                else if (w == kSqeHeaderWords + 5U) values[j] = reinterpret_cast<uint64_t>(src);
            }
            const uint32_t bb = head + word / kWqebbLanes;
            const uint32_t idx = bb & (depth - 1U);
            asc_stwt(reinterpret_cast<__gm__ ulonglong2*>(sq + static_cast<uint64_t>(idx) * kWqebbLanes + word % kWqebbLanes),
                     make_ulonglong2(values[0], values[1]));
        }
        asc_threadfence();
        if (kDoCommit) Commit();
        return 0;
    }

    template <typename Coop>
    __simt_callee__ __forceinline__ void Commit(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0U) Commit();
        coop.sync();
    }
    __simt_callee__ __forceinline__ void Commit() const
    {
        if (!implicitActive_ || implicitReservedWqebbs_ == 0U) return;
        auto* info = Info();
        const uint32_t end = static_cast<uint32_t>(*reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead));
        const uint32_t begin = implicitBeginHead_;
        if (end <= begin) return;
        const uint32_t reservedWqebbs = implicitReservedWqebbs_;
        if (reservedWqebbs > kSlotWqebbs || reservedWqebbs > end - begin) return;
        const uint32_t last = end - reservedWqebbs;
        const uint32_t payloadWqebbs = implicitPayloadWqebbs_ == 0U ? reservedWqebbs : implicitPayloadWqebbs_;
        if (payloadWqebbs > reservedWqebbs) return;
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        const uint32_t idx = last & (info->sqDepth - 1U);
        const uint64_t owner = static_cast<uint64_t>(detail::Owner(last, info->sqDepth)) << 31U;
        uint64_t header = kWriteHeader | owner | idx;
        if (payloadWqebbs == reservedWqebbs) {
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
        implicitActive_ = false;
        implicitReservedWqebbs_ = 0U;
        implicitPayloadWqebbs_ = 0U;
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
        Commit(coop);
    }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSqNoDoorbell(const Coop& coop) const
    {
        Commit(coop);
    }
    __simt_callee__ __forceinline__ void PublishSq() const
    {
        auto* info = Info();
        if (info == nullptr || info->sqHeadAddr == 0U || info->sqDoorbellAddr == 0U) return;
        info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
        *reinterpret_cast<__gm__ uint32_t*>(info->sqDoorbellAddr) = static_cast<uint32_t>(info->packedHead);
    }
    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const
    {
        auto* info = Info();
        if (info == nullptr || info->cqTailAddr == 0U) return -1;
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
    mutable uint32_t implicitReservedWqebbs_ = 0U;
    mutable uint32_t implicitPayloadWqebbs_ = 0U;
    mutable bool implicitActive_ = false;

    template <uint32_t kMaxSges>
    __simt_callee__ __forceinline__ int32_t WriteScalar(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        HcommSge one[kMaxSges] = {};
        one[0] = sge;
        return WriteScalarArray<kMaxSges>(peer, dst, one);
    }

    template <uint32_t kMaxSges>
    __simt_callee__ __forceinline__ static uint32_t CountValidSges(const HcommSge (&sges)[kMaxSges])
    {
        uint32_t count = 0U;
#pragma unroll
        for (uint32_t i = 0U; i < kMaxSges; ++i) {
            count += detail::Valid(sges[i]) ? 1U : 0U;
        }
        return count;
    }

    template <uint32_t kMaxSges>
    __simt_callee__ __forceinline__ static void GetValidSge(
        const HcommSge (&sges)[kMaxSges], const uint32_t ordinal, uint64_t& addr, int& len)
    {
        uint32_t seen = 0U;
#pragma unroll
        for (uint32_t i = 0U; i < kMaxSges; ++i) {
            if (detail::Valid(sges[i])) {
                if (seen == ordinal) {
                    addr = sges[i].addr;
                    len = sges[i].len;
                    return;
                }
                ++seen;
            }
        }
    }

    __simt_callee__ __forceinline__ static uint64_t BuildScalarWriteWord(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t word, const uint32_t sqIdx,
        const uint64_t owner, const uint32_t count, const uint32_t payloadWords, const uint64_t sgeAddr,
        const int sgeLen)
    {
        if (word == 0U) return kWriteHeader | owner | sqIdx;
        if (word < kSqeHeaderWords) return detail::PeerWord(peer.Info(), word, count, dst);
        if (word < payloadWords) {
            return ((word - kSqeHeaderWords) & 1U) == 0U ? static_cast<uint64_t>(sgeLen) : sgeAddr;
        }
        if ((word % kWqebbLanes) == 0U) return kNopHeader | owner | sqIdx;
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

    template <uint32_t kMaxSges>
    __simt_callee__ __forceinline__ int32_t WriteScalarArray(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kMaxSges]) const
    {
        static_assert(kMaxSges > 0U && kMaxSges <= 12U, "invalid scalar SGE count");
        const uint32_t count = CountValidSges(sges);
        if (count == 0U || count > kMaxSges) return -1;
        if (Reserve<kSlotWqebbs>() != 0) return -1;
        const uint32_t payloadWords = kSqeHeaderWords + count * 2U;
        implicitPayloadWqebbs_ = (payloadWords + kWqebbLanes - 1U) / kWqebbLanes;
        const uint32_t head = implicitBeginHead_;
        auto* info = Info();
        auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
        for (uint32_t firstWord = 0U; firstWord < kSlotWqebbs * kWqebbLanes; firstWord += 2U) {
            const uint32_t bbHead = head + firstWord / kWqebbLanes;
            const uint32_t idx = bbHead & (info->sqDepth - 1U);
            const uint64_t owner = static_cast<uint64_t>(detail::Owner(bbHead, info->sqDepth)) << 31U;
            uint64_t firstAddr = 0U;
            int firstLen = 0;
            if (firstWord >= kSqeHeaderWords && firstWord < payloadWords) {
                GetValidSge(sges, (firstWord - kSqeHeaderWords) / 2U, firstAddr, firstLen);
            }
            const uint32_t secondWord = firstWord + 1U;
            uint64_t secondAddr = 0U;
            int secondLen = 0;
            if (secondWord >= kSqeHeaderWords && secondWord < payloadWords) {
                GetValidSge(sges, (secondWord - kSqeHeaderWords) / 2U, secondAddr, secondLen);
            }
            const uint64_t first = BuildScalarWriteWord(
                peer, dst, firstWord, idx, owner, count, payloadWords, firstAddr, firstLen);
            const uint64_t second = BuildScalarWriteWord(
                peer, dst, secondWord, idx, owner, count, payloadWords, secondAddr, secondLen);
            StoreSqePair(sq, info->sqDepth, head, firstWord, first, second);
        }
        asc_threadfence();
        return 0;
    }
};

} // namespace AscendC::simt::jetty
} // namespace AscendC::simt

namespace AscendC::simt {
using jetty::HcommJettyInfo;
using jetty::HcommPeer;
using jetty::HcommPeerInfo;
using jetty::HcommSge;
using jetty::HcommJetty;
}

#endif
