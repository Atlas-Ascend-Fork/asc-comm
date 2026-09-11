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

#if defined(ASCENDC_DEBUG) || defined(HCOMM_SIMT_JETTY_DEBUG)
inline constexpr bool kDebugChecks = true;
#else
inline constexpr bool kDebugChecks = false;
#endif

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

template <uint32_t kSgeNum, HcommPostOpcode kOpcode>
__simt_callee__ constexpr uint32_t PostSendPayloadWqebbs()
{
    if constexpr (kOpcode == HcommPostOpcode::kWrite) {
        return (kSqeHeaderWords + kSgeNum * 2U + kWqebbLanes - 1U) / kWqebbLanes;
    } else if constexpr (kOpcode == HcommPostOpcode::kWriteWithNotify) {
        return 2U;
    } else {
        return 1U;
    }
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
        if constexpr (kDebugChecks) {
            if (table == 0U || info == nullptr) return;
        }
        const uint64_t channelAddr = *reinterpret_cast<__gm__ uint64_t*>(table + jettyIdx * sizeof(uint64_t));
        Attach(info, channelAddr);
        if constexpr (kDebugChecks) {
            if (channelAddr == 0U) return;
        }
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t rbAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, remoteBufferAddr));
        const uint32_t rbNum = detail::Load<uint32_t>(channelAddr, offsetof(ChannelEntity, remoteBufferNum));
        if constexpr (kDebugChecks) {
            if (sqCtxAddr == 0U || rbAddr == 0U || rbNum == 0U) return;
        }
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

    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    {
        const uint64_t tableAddr = reinterpret_cast<uint64_t>(table);
        if constexpr (kDebugChecks) {
            if (tableAddr == 0U || info == nullptr) return;
        }
        const uint64_t channelAddr =
            *reinterpret_cast<__gm__ uint64_t*>(tableAddr + index * sizeof(uint64_t));
        Attach(info, stage, channelAddr);
        if (stage != nullptr) {
            auto* batch = reinterpret_cast<__ubuf__ BatchState*>(stage);
            batch->wqebbCount = 0U;
            batch->lastOffset = 0U;
            batch->failed = 0U;
        }
        if constexpr (kDebugChecks) {
            if (channelAddr == 0U) return;
        }
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t cqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, cqContextAddr));
        if constexpr (kDebugChecks) {
            if (sqCtxAddr == 0U || cqCtxAddr == 0U) return;
        }
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
        if constexpr (kDebugChecks) {
            if (info->sqHeadAddr == 0U) return;
        }
        info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
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

private:
    __simt_callee__ __forceinline__ __ubuf__ BatchState* Batch() const
    {
        return reinterpret_cast<__ubuf__ BatchState*>(stageAddr_);
    }

public:

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
        const uint32_t mask = asc_ballot(active && detail::Valid(laneSge));
        const uint32_t count = static_cast<uint32_t>(__builtin_popcount(mask));
        return PostSend<kMaxSges, HcommPostOpcode::kWrite, kDoCommit>(
            coop, peer, dst, laneSge, nullptr, mask, count, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }

    template <uint32_t kMaxSges, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kMaxSges]) const
    {
        HcommCoopThread thread;
        const uint32_t count = CountValidSges(sges);
        return PostSend<kMaxSges, HcommPostOpcode::kWrite, kDoCommit>(
            thread, peer, dst, HcommSge{}, sges, 0U, count, nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }

private:
    template <uint32_t kSgeNum, HcommPostOpcode kOpcode, bool kDoCommit, typename Coop>
    __simt_callee__ __forceinline__ int32_t PostSend(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& laneSge,
        const HcommSge* scalarSges, const uint32_t mask, const uint32_t count, __gm__ void* src,
        const uint64_t len, __gm__ void* notifyAddr, const uint64_t notifyValue,
        const uint8_t* inlineValue, const uint32_t inlineBytes) const
    {
        static_assert(kOpcode == HcommPostOpcode::kWrite || kSgeNum == 0U,
                      "non-write posts do not carry SGEs");
        static_assert(kSgeNum <= kMaxSges, "invalid SGE count");
        static_assert(PostSendPayloadWqebbs<kSgeNum, kOpcode>() <= kSlotWqebbs,
                      "post-send payload exceeds the SQ slot");
        const uint32_t rank = static_cast<uint32_t>(coop.thread_rank());
        const uint32_t laneRank = rank % kWarpSize;
        if constexpr (kDoCommit) {
            if (coop.size() > static_cast<int>(kWarpSize)) {
                coop.sync();
                return -1;
            }
        }
        if (coop.size() == 1) {
            if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                if (count == 0U) return -1;
            }
        } else {
            const bool hasValidSge = kOpcode == HcommPostOpcode::kWrite && count != 0U;
            coop.sync();
            if (!hasValidSge) {
                coop.sync();
                return 0;
            }
        }

        const uint32_t payloadWords =
            kOpcode == HcommPostOpcode::kWrite ? kSqeHeaderWords + count * 2U :
            kOpcode == HcommPostOpcode::kWriteValue ? kSqeHeaderWords + (inlineBytes + 7U) / 8U :
            2U * kWqebbLanes;
        const uint32_t payloadWqebbs = (payloadWords + kWqebbLanes - 1U) / kWqebbLanes;
        uint32_t head = 0xFFFFFFFFU;
        if (laneRank == 0U) {
            auto* info = Info();
            if constexpr (kDoCommit) {
                // Immediate commit is a single-producer mode. Keep the uncontended
                // path to one head/tail check and pull CQ only when the SQ is full.
                auto* sqHead = reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead);
                auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
                const uint32_t depth = info->sqDepth;
                const uint32_t oldHead = *sqHead;
                uint32_t consumed = oldHead - *sqTail;
                while ((consumed > depth || payloadWqebbs > depth - consumed) && PullCqOne()) {
                    consumed = oldHead - *sqTail;
                }
                if (consumed <= depth && payloadWqebbs <= depth - consumed) {
                    head = oldHead;
                    *sqHead = oldHead + payloadWqebbs;
                    implicitBeginHead_ = oldHead;
                    implicitWqebbs_ = payloadWqebbs;
                    implicitPayloadWqebbs_ = payloadWqebbs;
                    implicitActive_ = true;
                }
            } else if (stageAddr_ != 0U) {
                // Deferred multi-warp mode mirrors dispatch: packedHead remains the
                // published base while each warp atomically claims a UB-local offset.
                auto* batch = Batch();
                const uint32_t offset = asc_atomic_add(&batch->wqebbCount, payloadWqebbs);
                const uint32_t base = static_cast<uint32_t>(info->packedHead);
                const uint32_t consumed = base -
                    *reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
                if (consumed <= info->sqDepth && offset <= info->sqDepth - consumed &&
                    payloadWqebbs <= info->sqDepth - consumed - offset) {
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

        const uint32_t depth = Info()->sqDepth;
        const uint32_t totalWords = payloadWqebbs * kWqebbLanes;
        const uint32_t workerCount = static_cast<uint32_t>(coop.size());
        const uint32_t stepWords = (coop.size() > 1 ? workerCount : 1U) * 2U;
        const uint32_t firstWord = (coop.size() > 1 ? rank : 0U) * 2U;
        for (uint32_t word = firstWord; word < totalWords; word += stepWords) {
            const uint32_t bbHead = head + word / kWqebbLanes;
            const uint32_t sqIdx = bbHead & (depth - 1U);
            const uint64_t owner = static_cast<uint64_t>(detail::Owner(bbHead, depth)) << 31U;
            const bool firstIsSge = kOpcode == HcommPostOpcode::kWrite &&
                word >= kSqeHeaderWords && word < payloadWords;
            const bool secondIsSge = kOpcode == HcommPostOpcode::kWrite &&
                word + 1U >= kSqeHeaderWords && word + 1U < payloadWords;
            uint64_t firstAddr = 0U;
            int firstLen = 0;
            uint64_t secondAddr = 0U;
            int secondLen = 0;
            if constexpr (kOpcode == HcommPostOpcode::kWrite && HcommCoopTraits<Coop>::kWarpCapable) {
                if (firstIsSge) {
                    const int ordinal = static_cast<int>((word - kSqeHeaderWords) / 2U);
                    detail::PackWarpSge(laneSge, mask, ordinal, firstAddr, firstLen);
                    secondAddr = firstAddr;
                    secondLen = firstLen;
                }
            } else if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                uint32_t seen = 0U;
                for (uint32_t i = 0U; i < kSgeNum; ++i) {
                    if (detail::Valid(scalarSges[i])) {
                        if (firstIsSge && seen == (word - kSqeHeaderWords) / 2U) {
                            firstAddr = scalarSges[i].addr;
                            firstLen = scalarSges[i].len;
                        }
                        if (secondIsSge && seen == (word - kSqeHeaderWords + 1U) / 2U) {
                            secondAddr = scalarSges[i].addr;
                            secondLen = scalarSges[i].len;
                        }
                        ++seen;
                    }
                }
            }
            const uint32_t secondWord = word + 1U;
            uint64_t first = 0U;
            uint64_t second = 0U;
            if constexpr (kOpcode == HcommPostOpcode::kWrite) {
                first = BuildWriteWord(peer, dst, word, sqIdx, owner, count, payloadWords, firstAddr, firstLen);
                second = BuildWriteWord(peer, dst, secondWord, sqIdx, owner, count, payloadWords, secondAddr, secondLen);
            } else if constexpr (kOpcode == HcommPostOpcode::kWriteValue) {
                first = BuildValueWord(peer, dst, word, sqIdx, owner, inlineValue, inlineBytes, payloadWords);
                second = BuildValueWord(peer, dst, secondWord, sqIdx, owner, inlineValue, inlineBytes, payloadWords);
            } else {
                first = BuildNotifyWord(peer, dst, word, sqIdx, owner, src, len, notifyAddr, notifyValue);
                second = BuildNotifyWord(peer, dst, secondWord, sqIdx, owner, src, len, notifyAddr, notifyValue);
            }
            StoreSqePair(reinterpret_cast<__gm__ uint64_t*>(Info()->sqBaseAddr), depth, head, word, first, second);
        }
        asc_threadfence();
        coop.sync();
        if constexpr (kDoCommit) Commit(coop);
        return 0;
    }

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
        if (laneRank == 0U) {
            auto* info = Info();
            auto* sqHead = reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead);
            auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
            const uint32_t depth = info->sqDepth;
            const uint32_t oldHead = *sqHead;
            uint32_t consumed = oldHead - *sqTail;
            while ((consumed > depth || 2U > depth - consumed) && PullCqOne()) {
                consumed = oldHead - *sqTail;
            }
            if (consumed <= depth && 2U <= depth - consumed) {
                head = oldHead;
                *sqHead = oldHead + 2U;
                implicitBeginHead_ = oldHead;
                implicitWqebbs_ = 2U;
                implicitPayloadWqebbs_ = count == 1U ? 1U : 2U;
                implicitActive_ = true;
            }
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
        HcommCoopThread thread;
        return PostSend<1U, HcommPostOpcode::kWrite, kDoCommit>(
            thread, peer, dst, HcommSge{}, &sge, 0U, detail::Valid(sge) ? 1U : 0U,
            nullptr, 0U, nullptr, 0U, nullptr, 0U);
    }

    template <int64_t, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
    {
        static_assert(sizeof(T) <= kWqebbBytes - kSqeHeaderWords * sizeof(uint64_t),
                      "inline value does not fit in a URMA WQEBB");
        HcommCoopThread thread;
        return PostSend<0U, HcommPostOpcode::kWriteValue, kDoCommit>(
            thread, peer, dst, HcommSge{}, nullptr, 0U, 0U, nullptr, 0U, nullptr, 0U,
            reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    template <int64_t, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len,
        __gm__ void* notifyAddr, uint64_t notifyValue)
    {
        HcommCoopThread thread;
        return PostSend<0U, HcommPostOpcode::kWriteWithNotify, kDoCommit>(
            thread, peer, dst, HcommSge{}, nullptr, 0U, 0U, src, len, notifyAddr, notifyValue, nullptr, 0U);
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
        if constexpr (kDebugChecks) {
            if (end <= begin || submittedWqebbs > kSlotWqebbs || submittedWqebbs > end - begin) return;
        }
        const uint32_t last = end - submittedWqebbs;
        const uint32_t payloadWqebbs = implicitPayloadWqebbs_ == 0U ? submittedWqebbs : implicitPayloadWqebbs_;
        if constexpr (kDebugChecks) {
            if (payloadWqebbs > submittedWqebbs) return;
        }
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
        if constexpr (kDebugChecks) {
            if (info == nullptr || info->sqHeadAddr == 0U || info->sqDoorbellAddr == 0U) return;
        }
        info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
        *reinterpret_cast<__gm__ uint32_t*>(info->sqDoorbellAddr) = static_cast<uint32_t>(info->packedHead);
    }
    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const
    {
        auto* info = Info();
        if constexpr (kDebugChecks) {
            if (info == nullptr || info->cqTailAddr == 0U || info->cqBaseAddr == 0U ||
                info->cqDepth == 0U || info->numCqeBytes == 0U || info->completionTailAddr == 0U ||
                info->cqDoorbellAddr == 0U) return -1;
        }
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

    __simt_callee__ __forceinline__ static uint64_t BuildWriteWord(
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

    __simt_callee__ __forceinline__ static uint64_t BuildValueWord(
        const HcommPeer& peer, __gm__ void* dst, const uint32_t word, const uint32_t sqIdx,
        const uint64_t owner, const uint8_t* value, const uint32_t valueBytes, const uint32_t payloadWords)
    {
        if (word == 0U) {
            return kWriteHeader | owner | sqIdx | (1ULL << 22U) |
                (static_cast<uint64_t>(valueBytes & 0x3FFU) << 54U);
        }
        if (word < kSqeHeaderWords) return detail::PeerWord(peer.Info(), word, 0U, dst);
        if (word < payloadWords) {
            const uint32_t offset = (word - kSqeHeaderWords) * sizeof(uint64_t);
            uint64_t result = 0U;
            for (uint32_t byte = 0U; byte < sizeof(uint64_t) && offset + byte < valueBytes; ++byte) {
                result |= static_cast<uint64_t>(value[offset + byte]) << (byte * 8U);
            }
            return result;
        }
        if ((word % kWqebbLanes) == 0U) return kNopHeader | owner | sqIdx;
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
