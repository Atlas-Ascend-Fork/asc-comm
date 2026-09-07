/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. Please make sure you use this file only in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software directory for the full text of the License.
 */

/*!
 * \file hcomm_jetty.h
 * \brief Direct URMA Jetty types for low-level SQ/CQ management.
 *
 * AscendC::simt::jetty::HcommJetty exposes a fixed-SGE WQE builder with cooperative-group wrappers
 * so a thread / warp / group can participate in constructing and publishing WQEs.
 */

#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H

#include <cassert>
#include <cstdint>

#include "hcomm.h"
#include <kernel_operator.h>
#include <c_api/sys_var/sys_var.h>

#if defined(USE_SIMT)
#include <simt_api/device_atomic_functions.h>
#include <simt_api/device_functions.h>
#include <simt_api/device_warp_functions.h>
#include <simt_api/vector_functions.h>
#include <simt_api/cooperative_groups.h>
#endif

#if defined(__CCE__)
#define HCOMM_JETTY_ASSERT(cond) \
    do {                         \
        if (!(cond)) {           \
            return;              \
        }                        \
    } while (0)
#else
#define HCOMM_JETTY_ASSERT(cond) assert(cond)
#endif

namespace AscendC {

constexpr int kNumMaxSGEPairsPerSQE = 6;
constexpr int kNumWQEBBBytes = 64;

constexpr uint64_t kNopSQEHeaderTemplate = 0x0000110000000000ULL;
constexpr uint64_t kWriteSQEHeaderTemplate = 0x0000030030010000ULL;
constexpr uint64_t kWriteSQEFinalFlags = (1ULL << 18) | (1ULL << 19) | (1ULL << 21);

struct alignas(8) HcommPeerInfo {
    uint64_t tp_id_num_sges_rmt_token_id;
    uint64_t rmt_eid[2];
    uint64_t rmt_token_value_udf;
    uint64_t rmt_base_addr;
    uint64_t rmt_buffer_size;
};
static_assert(sizeof(HcommPeerInfo) == 48, "HcommPeerInfo size mismatch");
static_assert(alignof(HcommPeerInfo) == 8, "HcommPeerInfo alignment mismatch");

struct alignas(8) HcommJettyInfo {
    uint64_t sq_base_addr;
    uint64_t sq_head_addr;
    uint64_t completion_tail_addr;
    uint64_t sq_doorbell_addr;
    uint64_t cq_base_addr;
    uint64_t cq_tail_addr;
    uint64_t cq_doorbell_addr;
    uint64_t dwqe_addr;
    uint64_t packed_head;
    uint32_t num_wqebb_bytes;
    uint32_t num_cqe_bytes;
    uint32_t sq_depth;
    uint32_t cq_depth;
    int32_t pending_writes;
};
static_assert(sizeof(HcommJettyInfo) == 96, "HcommJettyInfo size mismatch");
static_assert(alignof(HcommJettyInfo) == 8, "HcommJettyInfo alignment mismatch");

namespace jetty_detail {

template <typename dtype_t>
constexpr __aicore__ inline dtype_t Min(dtype_t a, dtype_t b)
{
    return a < b ? a : b;
}

template <typename dtype_t>
constexpr __aicore__ inline bool IsPowerOf2(dtype_t value)
{
    return value > 0 and (value & (value - 1)) == 0;
}

} // namespace jetty_detail

#if defined(USE_SIMT)

namespace simt {

struct HcommCoopThread {
    __simt_callee__ inline void sync() const {}
    __simt_callee__ inline int size() const { return 1; }
    __simt_callee__ inline int thread_rank() const
    {
        return 0;
    }
};

struct HcommCoopWarp {
    cooperative_groups::thread_block_tile<32, cooperative_groups::thread_block> group_;

    __simt_callee__ inline HcommCoopWarp()
        : group_(cooperative_groups::tiled_partition<32>(cooperative_groups::this_thread_block()))
    {}

    __simt_callee__ inline void sync() const
    {
        group_.sync();
    }
    __simt_callee__ inline int size() const
    {
        return 32;
    }
    __simt_callee__ inline int thread_rank() const
    {
        return static_cast<int>(group_.thread_rank());
    }
};

struct HcommCoopGroup {
    cooperative_groups::thread_block group_;

    __simt_callee__ inline HcommCoopGroup()
        : group_(cooperative_groups::this_thread_block())
    {}

    __simt_callee__ inline void sync() const
    {
        group_.sync();
    }
    __simt_callee__ inline int size() const
    {
        return static_cast<int>(group_.size());
    }

    __simt_callee__ inline int thread_rank() const
    {
        return static_cast<int>(group_.thread_rank());
    }
};

namespace jetty {

struct alignas(8) HcommSge {
    uint64_t addr = 0;
    int len = 0;
};
static_assert(sizeof(HcommSge) == 16, "SIMT HcommSge size mismatch");
static_assert(alignof(HcommSge) == 8, "SIMT HcommSge alignment mismatch");

struct HcommPeer {
    __ubuf__ HcommPeerInfo* peer_info = nullptr;

    __simt_callee__ inline HcommPeer() = default;

    __simt_callee__ inline explicit HcommPeer(__ubuf__ HcommPeerInfo* peer_info):
        peer_info(peer_info)
    {}

    __simt_callee__ inline HcommPeer(
            __ubuf__ HcommPeerInfo* peer_info,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        peer_info(peer_info)
    {
        Init(jetty_ptrs, jetty_idx);
    }

    __simt_callee__ inline void Init(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = *(jetty_table + jetty_idx);
        HCOMM_JETTY_ASSERT(jetty_value != 0);
        const auto jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
        HCOMM_JETTY_ASSERT(jetty->remoteBufferNum > 0);
        const auto remote_buffer = jetty->remoteBufferAddr[jetty->remoteBufferNum - 1];
        peer_info->rmt_token_value_udf = remote_buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        peer_info->rmt_base_addr = remote_buffer.bufferInfo.rma.addr;
        peer_info->rmt_buffer_size = remote_buffer.bufferInfo.rma.size;

        const auto sq_context = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        peer_info->tp_id_num_sges_rmt_token_id =
            static_cast<uint64_t>(sq_context.contextInfo.ubJfs.tpID) |
            (static_cast<uint64_t>(remote_buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32);
        peer_info->rmt_eid[0] = 0;
        peer_info->rmt_eid[1] = 0;
        for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
            peer_info->rmt_eid[0] |=
                static_cast<uint64_t>(sq_context.contextInfo.ubJfs.remoteEID[byte_idx]) << (byte_idx * 8);
            peer_info->rmt_eid[1] |=
                static_cast<uint64_t>(sq_context.contextInfo.ubJfs.remoteEID[byte_idx + 8]) << (byte_idx * 8);
        }
    }
};
struct HcommJetty {
    static constexpr int kWarpSize = 32;
    static constexpr int kNumMaxSGEsPerWrite = 2 * kNumMaxSGEPairsPerSQE;
    static constexpr int kNumWQEBBsPerWrite =
        (sizeof(HcommUrmaSqeCtx) + kNumMaxSGEsPerWrite * sizeof(HcommUrmaSgeCtx) + kNumWQEBBBytes - 1) /
        kNumWQEBBBytes;
    static constexpr int kNumWQEBBLanes = kNumWQEBBBytes / sizeof(uint64_t);
    static constexpr int kNumWriteHeaderLanes = sizeof(HcommUrmaSqeCtx) / sizeof(uint64_t);
    static constexpr uint32_t kNumSimtWriteWQEBBs = kNumWQEBBsPerWrite;
    static constexpr uint32_t kNumMaxSQEBytes = kNumWQEBBsPerWrite * kNumWQEBBBytes;
    static constexpr int64_t kDefaultTimeoutCycles = 1000000LL;

    static_assert(kNumWriteHeaderLanes == 6, "SIMT URMA SQE header must contain six 64-bit words");
    static_assert(kNumWQEBBsPerWrite == 4, "SIMT URMA write reservation must contain four WQEBBs");
    static_assert(kNumMaxSGEsPerWrite <= kWarpSize - kNumWriteHeaderLanes,
                  "SIMT URMA SGE layout must fit in one warp");

    __ubuf__ HcommJettyInfo* jetty_info = nullptr;

    __simt_callee__ inline HcommJetty() = default;

    __simt_callee__ inline explicit HcommJetty(__ubuf__ HcommJettyInfo* jetty_info):
        jetty_info(jetty_info)
    {}

    __simt_callee__ inline HcommJetty(
            __ubuf__ HcommJettyInfo* jetty_info,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        HcommJetty(jetty_info)
    {
        Init(jetty_ptrs, jetty_idx);
    }

    __simt_callee__ inline void Init(
            __ubuf__ HcommJettyInfo* jetty_info,
            __gm__ void* jetty_ptrs, const int jetty_idx)
    {
        this->jetty_info = jetty_info;
        Init(jetty_ptrs, jetty_idx);
    }

    // Load queue metadata once from the cooperative caller's lane 0.
    __simt_callee__ inline void Init(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        constexpr uint32_t kDwqeDbOffset = 0x80U;
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = *(jetty_table + jetty_idx);
        HCOMM_JETTY_ASSERT(jetty_value != 0);
        const auto jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
        HCOMM_JETTY_ASSERT(jetty->sqNum > 0 && jetty->cqNum > 0);

        const auto sq_context = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        const auto cq_context = jetty->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        jetty_info->sq_base_addr = sq_context.contextInfo.ubJfs.sqVa;
        jetty_info->sq_head_addr = sq_context.contextInfo.ubJfs.headAddr;
        jetty_info->completion_tail_addr = sq_context.contextInfo.ubJfs.tailAddr;
        jetty_info->sq_doorbell_addr = sq_context.contextInfo.ubJfs.dbVa;
        jetty_info->dwqe_addr = sq_context.contextInfo.ubJfs.dbVa - kDwqeDbOffset;
        jetty_info->cq_base_addr = cq_context.contextInfo.ubJfc.scqVa;
        jetty_info->cq_tail_addr = cq_context.contextInfo.ubJfc.tailAddr;
        jetty_info->cq_doorbell_addr = cq_context.contextInfo.ubJfc.dbVa;
        jetty_info->packed_head = *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr);
        jetty_info->num_wqebb_bytes = sq_context.contextInfo.ubJfs.wqeSize;
        jetty_info->num_cqe_bytes = cq_context.contextInfo.ubJfc.cqeSize;
        jetty_info->sq_depth = sq_context.contextInfo.ubJfs.sqDepth;
        jetty_info->cq_depth = cq_context.contextInfo.ubJfc.cqDepth;
        jetty_info->pending_writes = 0;
        HCOMM_JETTY_ASSERT(jetty_info->num_wqebb_bytes == kNumWQEBBBytes);
        HCOMM_JETTY_ASSERT(jetty_detail::IsPowerOf2(jetty_info->sq_depth));
        HCOMM_JETTY_ASSERT(jetty_info->sq_depth >= kNumWQEBBsPerWrite);
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ inline void PollCqWhenSqOverflow() const
    {
        static constexpr uint32_t kNumPollCQThreshold = 10;
        static constexpr uint32_t kNumCQEsPerPoll = 100;
        const auto num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32);
        const auto completion_tail = *reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr);
        if ((num_completions + kNumPollCQThreshold) % jetty_info->cq_depth ==
            completion_tail % jetty_info->cq_depth) {
            PollCq<kNumTimeoutCycles>(jetty_detail::Min(completion_tail + kNumCQEsPerPoll, num_completions));
        }
    }

    template <int kNumSGEs>
    __simt_callee__ inline void PostWriteWarp(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");

        const int lane_idx = static_cast<int>(threadIdx.x) % kWarpSize;
        const auto peer_info_words = reinterpret_cast<__ubuf__ uint64_t*>(peer.peer_info);
        int write_idx = 0;
        if (lane_idx == 0) {
            PollCqWhenSqOverflow<kDefaultTimeoutCycles>();
            write_idx = atomicAdd(
                reinterpret_cast<__ubuf__ int32_t*>(&jetty_info->pending_writes), static_cast<int32_t>(1));
        }
        write_idx = __shfl(write_idx, 0, kWarpSize);
        HCOMM_JETTY_ASSERT(write_idx >= 0);
        HCOMM_JETTY_ASSERT((write_idx + 1) * kNumWQEBBsPerWrite <= static_cast<int>(jetty_info->sq_depth));

        const uint32_t current_head = static_cast<uint32_t>(jetty_info->packed_head) +
            static_cast<uint32_t>(write_idx) * kNumWQEBBsPerWrite;
        const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
        const uint32_t sq_wqebb_idx = wqebb_head % jetty_info->sq_depth;
        const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & jetty_info->sq_depth) == 0) << 31;
        const int raw_sge_lane = (lane_idx - kNumWriteHeaderLanes) / 2;
        const int sge_lane = raw_sge_lane < 0 ? 0 :
            (raw_sge_lane >= kNumMaxSGEsPerWrite ? kNumMaxSGEsPerWrite - 1 : raw_sge_lane);
        const uint64_t shuffled_sge_addr = __shfl(sge.addr, sge_lane, kWarpSize);
        const int shuffled_sge_len = __shfl(sge.len, sge_lane, kWarpSize);

        uint64_t sqe_word = 0;
        if (lane_idx == 0) {
            sqe_word = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        } else if (lane_idx < kNumWriteHeaderLanes) {
            sqe_word = peer_info_words[lane_idx - 1];
            if (lane_idx == 1) {
                sqe_word |= static_cast<uint64_t>(kNumSGEs) << 24;
            }
            if (lane_idx == 5) {
                sqe_word = reinterpret_cast<uint64_t>(dst);
            }
        } else if (lane_idx < kNumWriteHeaderLanes + kNumSGEs * 2) {
            const bool is_len_word = ((lane_idx - kNumWriteHeaderLanes) % 2) == 0;
            const bool valid_sge = shuffled_sge_addr != 0 && shuffled_sge_len > 0;
            sqe_word = valid_sge ? (is_len_word ? static_cast<uint64_t>(shuffled_sge_len) : shuffled_sge_addr) : 0;
        } else if (lane_idx % kNumWQEBBLanes == 0) {
            // Unused BBs in the fixed four-BB reservation are real NOP WQEs.
            sqe_word = kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        }

        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_base_addr);
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
            sq_words + static_cast<uint64_t>(sq_wqebb_idx) * kNumWQEBBLanes + lane_idx % kNumWQEBBLanes,
            sqe_word);
    }

    template <int kNumSGEs>
    __simt_callee__ inline void PostWriteScalar(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs])
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");
        PollCqWhenSqOverflow<kDefaultTimeoutCycles>();
        const int write_idx = atomicAdd(
            reinterpret_cast<__ubuf__ int32_t*>(&jetty_info->pending_writes), static_cast<int32_t>(1));
        HCOMM_JETTY_ASSERT(write_idx >= 0);
        HCOMM_JETTY_ASSERT((write_idx + 1) * kNumWQEBBsPerWrite <= static_cast<int>(jetty_info->sq_depth));

        const uint32_t current_head = static_cast<uint32_t>(jetty_info->packed_head) +
            static_cast<uint32_t>(write_idx) * kNumWQEBBsPerWrite;
        const auto peer_info_words = reinterpret_cast<__ubuf__ uint64_t*>(peer.peer_info);
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_base_addr);
        for (int lane_idx = 0; lane_idx < kWarpSize; ++lane_idx) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head % jetty_info->sq_depth;
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & jetty_info->sq_depth) == 0) << 31;
            uint64_t sqe_word = 0;
            if (lane_idx == 0) {
                sqe_word = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
            } else if (lane_idx < kNumWriteHeaderLanes) {
                sqe_word = peer_info_words[lane_idx - 1];
                if (lane_idx == 1) {
                    sqe_word |= static_cast<uint64_t>(kNumSGEs) << 24;
                }
                if (lane_idx == 5) {
                    sqe_word = reinterpret_cast<uint64_t>(dst);
                }
            } else if (lane_idx < kNumWriteHeaderLanes + kNumSGEs * 2) {
                const int sge_idx = (lane_idx - kNumWriteHeaderLanes) / 2;
                const auto& scalar_sge = sges[sge_idx];
                const bool is_len_word = ((lane_idx - kNumWriteHeaderLanes) % 2) == 0;
                const bool valid_sge = scalar_sge.addr != 0 && scalar_sge.len > 0;
                sqe_word = valid_sge ?
                    (is_len_word ? static_cast<uint64_t>(scalar_sge.len) : scalar_sge.addr) : 0;
            } else if (lane_idx % kNumWQEBBLanes == 0) {
                sqe_word = kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
            }
            __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
                sq_words + static_cast<uint64_t>(sq_wqebb_idx) * kNumWQEBBLanes + lane_idx % kNumWQEBBLanes,
                sqe_word);
        }
    }

    template <int kNumSGEs>
    __simt_callee__ inline void PostWriteScalar(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge& first_sge)
    {
        HcommSge sges[kNumSGEs] = {};
        sges[0] = first_sge;
        PostWriteScalar<kNumSGEs>(peer, dst, sges);
    }

    template <int kNumSGEs>
    __simt_callee__ inline void Write(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sge);
    }

    template <int kNumSGEs>
    __simt_callee__ inline void Write(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sges);
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ inline void Write(
            const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        coop.sync();
        auto* self = const_cast<HcommJetty*>(this);
        if (coop.size() >= kWarpSize) {
            if (coop.thread_rank() < kWarpSize) {
                self->template PostWriteWarp<kNumSGEs>(peer, dst, sge);
            }
        } else if (coop.thread_rank() == 0) {
            self->template PostWriteScalar<kNumSGEs>(peer, dst, sge);
        }
        coop.sync();
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ inline void Write(
            const Coop& coop, const HcommPeer& peer, __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sges);
        }
        coop.sync();
    }

    template <int kNumSGEs, bool kDoCommit = true, typename Coop>
    __simt_callee__ inline void Put(
            const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        Write<kNumSGEs>(coop, peer, dst, sge);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true, typename Coop>
    __simt_callee__ inline void Put(
            const Coop& coop, const HcommPeer& peer, __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        Write<kNumSGEs>(coop, peer, dst, sges);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ inline void Put(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        Write<kNumSGEs>(peer, dst, sge);
        if constexpr (kDoCommit) {
            const_cast<HcommJetty*>(this)->Commit();
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ inline void Put(
            const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        Write<kNumSGEs>(peer, dst, sges);
        if constexpr (kDoCommit) {
            const_cast<HcommJetty*>(this)->Commit();
        }
    }

    template <typename Coop>
    __simt_callee__ inline void Commit(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->Commit();
        }
        coop.sync();
    }

    __simt_callee__ inline void Commit()
    {
        const auto total_writes = jetty_info->pending_writes;
        if (total_writes == 0) {
            return;
        }
        HCOMM_JETTY_ASSERT(total_writes * kNumWQEBBsPerWrite <= static_cast<int>(jetty_info->sq_depth));

        const uint32_t old_head = static_cast<uint32_t>(jetty_info->packed_head);
        const uint32_t new_head = old_head + static_cast<uint32_t>(total_writes) * kNumWQEBBsPerWrite;
        const uint32_t new_num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32) + 1;
        const uint64_t new_packed_head = static_cast<uint64_t>(new_head) |
            (static_cast<uint64_t>(new_num_completions) << 32);

        const uint32_t last_write_head = old_head +
            static_cast<uint32_t>(total_writes - 1) * kNumWQEBBsPerWrite;
        const uint32_t last_write_idx = last_write_head % jetty_info->sq_depth;
        auto* last_write_header = reinterpret_cast<__gm__ uint64_t*>(
            jetty_info->sq_base_addr + static_cast<uint64_t>(jetty_info->num_wqebb_bytes) * last_write_idx);
        const auto owner_bit = static_cast<uint64_t>((last_write_head & jetty_info->sq_depth) == 0) << 31;
        const auto final_header = kWriteSQEHeaderTemplate | owner_bit |
            static_cast<uint64_t>(last_write_idx) | kWriteSQEFinalFlags;
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(last_write_header, final_header);

        asc_threadfence();
        *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr) = new_packed_head;
        auto* dwqe_words = reinterpret_cast<__gm__ ulonglong2*>(jetty_info->dwqe_addr);
        const uint32_t last_wqebb_head = old_head +
            static_cast<uint32_t>(total_writes) * kNumWQEBBsPerWrite - 1U;
        const uint32_t last_sq_idx = last_wqebb_head % jetty_info->sq_depth;
        const auto* last_sq_words = reinterpret_cast<__gm__ uint64_t*>(
            jetty_info->sq_base_addr + static_cast<uint64_t>(jetty_info->num_wqebb_bytes) * last_sq_idx);
        for (uint32_t pair = 0; pair < kNumWQEBBBytes / sizeof(ulonglong2); ++pair) {
            const uint32_t word = pair * 2U;
            __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
                &dwqe_words[pair], make_ulonglong2(last_sq_words[word], last_sq_words[word + 1U]));
        }
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
            &dwqe_words[4], make_ulonglong2(new_packed_head, 0ULL));
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
            &dwqe_words[5], make_ulonglong2(0ULL, 0ULL));
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
            &dwqe_words[6], make_ulonglong2(0ULL, 0ULL));
        __stg<ST_L2CacheType::L2_CACHE_HINT_NORMAL_FV, L1CacheType::CACHEABLE>(
            &dwqe_words[7], make_ulonglong2(static_cast<uint64_t>(new_head), 0ULL));
        __asc_simt_vf::asc_dcci_single(reinterpret_cast<__gm__ void*>(jetty_info->dwqe_addr));
        asc_dcci_single(reinterpret_cast<__gm__ uint8_t*>(jetty_info->dwqe_addr) + kNumWQEBBBytes);
        jetty_info->packed_head = new_packed_head;
        jetty_info->pending_writes = 0;
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ inline void Drain() const
    {
        PollCq<kNumTimeoutCycles>(
            static_cast<uint32_t>(*reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr) >> 32));
    }

    template <int64_t kNumTimeoutCycles, typename Coop>
    __simt_callee__ inline void Drain(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            Drain<kNumTimeoutCycles>();
        }
        coop.sync();
    }

private:
    template <int64_t kNumTimeoutCycles>
    __simt_callee__ inline void PollCq(const uint32_t expected_tail) const
    {
        static constexpr uint32_t kNumCQDoorbellBits = 24;
        static constexpr uint32_t kCQEOwnerBit = 2;
        auto current_tail = *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr);
        while (current_tail != expected_tail) {
            const auto cqe = reinterpret_cast<__gm__ uint32_t*>(
                jetty_info->cq_base_addr + static_cast<uint64_t>(jetty_info->num_cqe_bytes) *
                (current_tail & (jetty_info->cq_depth - 1)));
            const auto tail_phase = (current_tail / jetty_info->cq_depth) & 1;
            while (true) {
                const auto cqe_header = *cqe;
                if (((cqe_header >> kCQEOwnerBit) & 1) != tail_phase) {
                    const auto substatus = static_cast<uint8_t>(cqe_header >> 16);
                    const auto status = static_cast<uint8_t>(cqe_header >> 24);
                    HCOMM_JETTY_ASSERT(status == 0 && substatus == 0);
                    break;
                }
            }
            ++current_tail;
        }
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr) = current_tail;
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_doorbell_addr) =
            current_tail & ((1U << kNumCQDoorbellBits) - 1);
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr) = current_tail;
    }
};

} // namespace jetty
} // namespace simt

#endif

#undef HCOMM_JETTY_ASSERT

} // namespace AscendC

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
