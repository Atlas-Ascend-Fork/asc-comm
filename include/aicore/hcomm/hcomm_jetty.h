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
 * AscendC::HcommJetty (AIV path) keeps the current scalar SQE staging flow.
 * AscendC::simt::jetty::HcommJetty (SIMT path) exposes a fixed-SGE WQE builder with cooperative-group
 * wrappers so one lane can publish a WQE while a thread / warp / group participates in the call.
 */

#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H

#include <cassert>
#include <cstdint>

#include "hcomm.h"
#include <kernel_operator.h>
#include <c_api/sys_var/sys_var.h>
#include <simt_api/device_functions.h>
#include <simt_api/vector_functions.h>

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

#if !defined(USE_SIMT)
#include <c_api/asc_simd.h>
#include <c_api/sync/sync.h>
#include <c_api/vector_datamove/vector_datamove.h>
#include <simt_api/device_functions.h>
#include <simt_api/device_warp_functions.h>
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
};
static_assert(sizeof(HcommJettyInfo) == 88, "HcommJettyInfo size mismatch");
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

#if defined(__CCE__) && !defined(USE_SIMT)

struct HcommPeer {
    __ubuf__ HcommPeerInfo* peer_info = nullptr;

    __aicore__ inline HcommPeer(
            __ubuf__ HcommPeerInfo* peer_info,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        peer_info(peer_info)
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
    static constexpr uint32_t kNumSimtWriteWQEBBs = 4;
    static constexpr uint32_t kNumSimtWQEWords = kNumSimtWriteWQEBBs * (kNumWQEBBBytes / sizeof(uint64_t));
    static constexpr uint32_t kNumMaxSQEBytes = kNumSimtWriteWQEBBs * kNumWQEBBBytes;
    static constexpr int64_t kDefaultTimeoutCycles = 1000000LL;

    __ubuf__ HcommJettyInfo* jetty_info = nullptr;
    __ubuf__ int* write_counter = nullptr;
    __ubuf__ int* num_writes = nullptr;
    __ubuf__ uint8_t* ub_sqe = nullptr;
    uint64_t dwqe_stage[32] = {};

    __aicore__ inline HcommJetty(
            __ubuf__ HcommJettyInfo* jetty_info,
            __ubuf__ int* write_counter,
            __ubuf__ int* num_writes,
            __ubuf__ uint8_t* ub_sqe,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        jetty_info(jetty_info),
        write_counter(write_counter),
        num_writes(num_writes),
        ub_sqe(ub_sqe)
    {
        constexpr uint32_t kDwqeDbOffset = 0x80U;
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = *(jetty_table + jetty_idx);
        HCOMM_JETTY_ASSERT(jetty_value != 0);
        const auto jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
        const auto sq_context = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        const auto cq_context = jetty->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        jetty_info->sq_base_addr = sq_context.contextInfo.ubJfs.sqVa;
        jetty_info->sq_head_addr = sq_context.contextInfo.ubJfs.headAddr;
        jetty_info->completion_tail_addr = sq_context.contextInfo.ubJfs.tailAddr;
        jetty_info->sq_doorbell_addr = sq_context.contextInfo.ubJfs.dbVa;
        jetty_info->dwqe_addr = sq_context.contextInfo.ubJfs.dbVa - kDwqeDbOffset;
        jetty_info->sq_depth = sq_context.contextInfo.ubJfs.sqDepth;
        jetty_info->num_wqebb_bytes = sq_context.contextInfo.ubJfs.wqeSize;
        jetty_info->cq_base_addr = cq_context.contextInfo.ubJfc.scqVa;
        jetty_info->cq_tail_addr = cq_context.contextInfo.ubJfc.tailAddr;
        jetty_info->cq_doorbell_addr = cq_context.contextInfo.ubJfc.dbVa;
        jetty_info->num_cqe_bytes = cq_context.contextInfo.ubJfc.cqeSize;
        jetty_info->cq_depth = cq_context.contextInfo.ubJfc.cqDepth;
        jetty_info->packed_head = *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr);
        HCOMM_JETTY_ASSERT(jetty_info->num_wqebb_bytes == kNumWQEBBBytes);
        HCOMM_JETTY_ASSERT(jetty_detail::IsPowerOf2(jetty_info->sq_depth));
        HCOMM_JETTY_ASSERT(jetty_info->sq_depth >= kNumSimtWriteWQEBBs);
    }

    template <int64_t kNumTimeoutCycles>
    __aicore__ inline void PollCqWhenSqOverflow()
    {
        static constexpr uint32_t kNumPollCQThreshold = 10;
        static constexpr uint32_t kNumCQEsPerPoll = 100;
        const auto num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32);
        const auto completion_tail = *reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr);

        if ((num_completions + kNumPollCQThreshold) % jetty_info->cq_depth ==
            completion_tail % jetty_info->cq_depth) {
            PollCq<kNumTimeoutCycles>(
                jetty_detail::Min(completion_tail + kNumCQEsPerPoll, num_completions));
        }
    }

    template <int kNumSGEs>
    __aicore__ inline void BuildWriteSqe(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs],
            const uint32_t current_head)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEPairsPerSQE * 2, "SIMT jetty SGE count exceeds the SQE layout");

        const auto peer_info_words = reinterpret_cast<__ubuf__ uint64_t*>(peer.peer_info);
        auto* sqe_words = reinterpret_cast<__ubuf__ uint64_t*>(ub_sqe);
        for (uint32_t i = 0; i < kNumSimtWQEWords; ++i) {
            sqe_words[i] = 0;
            dwqe_stage[i] = 0;
        }

        const auto sq_wqebb_idx = current_head % jetty_info->sq_depth;
        const auto owner_bit = static_cast<uint64_t>((current_head & jetty_info->sq_depth) == 0) << 31;

        sqe_words[0] = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        sqe_words[1] = peer_info_words[0] | (static_cast<uint64_t>(kNumSGEs) << 24);
        sqe_words[2] = peer_info_words[1];
        sqe_words[3] = peer_info_words[2];
        sqe_words[4] = peer_info_words[3];
        sqe_words[5] = reinterpret_cast<uint64_t>(dst);

        constexpr uint32_t kSgeWordBase = 6;
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            const auto& sge = sges[sge_idx];
            const uint32_t word_idx = kSgeWordBase + static_cast<uint32_t>(sge_idx) * 2U;
            const uint64_t len = (sge.addr == 0 || sge.len <= 0) ? 0ULL : static_cast<uint64_t>(sge.len);
            const uint64_t addr = (sge.addr == 0 || sge.len <= 0) ? 0ULL : sge.addr;
            sqe_words[word_idx] = len;
            sqe_words[word_idx + 1U] = addr;
        }

        for (uint32_t i = 0; i < kNumSimtWQEWords; ++i) {
            dwqe_stage[i] = sqe_words[i];
        }
    }

    template <int kNumSGEs>
    __aicore__ inline void CopySqeToSq(const uint32_t current_head)
    {
        const auto sq_idx = current_head & (jetty_info->sq_depth - 1);
        const auto num_first_wqebbs =
            jetty_detail::Min<uint32_t>(kNumSimtWriteWQEBBs, jetty_info->sq_depth - sq_idx);

        asc_sync_notify(PIPE_S, PIPE_MTE3, EVENT_ID0);
        asc_sync_wait(PIPE_S, PIPE_MTE3, EVENT_ID0);
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(
                jetty_info->sq_base_addr + static_cast<uint64_t>(jetty_info->num_wqebb_bytes) * sq_idx),
            ub_sqe,
            1, jetty_info->num_wqebb_bytes * num_first_wqebbs,
            static_cast<uint8_t>(ST_L2CacheType::L2_CACHE_HINT_NOTALLOC_CI),
            0, 0);

        if (num_first_wqebbs < kNumSimtWriteWQEBBs) {
            asc_copy_ub2gm_align(
                reinterpret_cast<__gm__ uint8_t*>(jetty_info->sq_base_addr),
                ub_sqe + jetty_info->num_wqebb_bytes * num_first_wqebbs,
                1, jetty_info->num_wqebb_bytes * (kNumSimtWriteWQEBBs - num_first_wqebbs),
                static_cast<uint8_t>(ST_L2CacheType::L2_CACHE_HINT_NOTALLOC_CI),
                0, 0);
        }
        asc_sync_notify(PIPE_MTE3, PIPE_S, EVENT_ID0);
        asc_sync_wait(PIPE_MTE3, PIPE_S, EVENT_ID0);
    }

    __aicore__ inline void AdvanceSq(const uint32_t num_wqebbs)
    {
        const auto new_head = static_cast<uint32_t>(jetty_info->packed_head) + num_wqebbs;
        const auto new_num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32) + 1;
        jetty_info->packed_head = static_cast<uint64_t>(new_head) |
            (static_cast<uint64_t>(new_num_completions) << 32);
        // Mirror the published head back to GM so Drain/PollCq observe the same producer state.
        *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr) = jetty_info->packed_head;
    }

    __aicore__ inline void RingDoorbell()
    {
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->sq_doorbell_addr) = static_cast<uint32_t>(jetty_info->packed_head);
    }

    template <int64_t kNumTimeoutCycles, int kNumSGEs, bool kDoCommit>
    __aicore__ inline void PostWrite(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs])
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        HCOMM_JETTY_ASSERT(*num_writes > 0);
        HCOMM_JETTY_ASSERT((*num_writes) * static_cast<int>(kNumSimtWriteWQEBBs) <= static_cast<int>(jetty_info->sq_depth));

        PollCqWhenSqOverflow<kNumTimeoutCycles>();
        const int write_idx = asc_atomic_add(
            reinterpret_cast<__ubuf__ int32_t*>(write_counter), static_cast<int32_t>(1));
        HCOMM_JETTY_ASSERT(write_idx >= 0);
        HCOMM_JETTY_ASSERT(write_idx < *num_writes);

        const auto current_head = static_cast<uint32_t>(jetty_info->packed_head) +
            static_cast<uint32_t>(write_idx) * kNumSimtWriteWQEBBs;

        BuildWriteSqe<kNumSGEs>(peer, dst, sges, current_head);
        CopySqeToSq<kNumSGEs>(current_head);

        if constexpr (kDoCommit) {
            AdvanceSq(kNumSimtWriteWQEBBs);
            RingDoorbell();
        }
    }

    template <int kNumSGEs>
    __aicore__ inline void Write(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        const_cast<HcommJetty*>(this)->template PostWrite<kDefaultTimeoutCycles, kNumSGEs, false>(peer, dst, sges);
    }

    template <int kNumSGEs, typename Coop>
    __aicore__ inline void Write(
            const Coop& coop,
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->template PostWrite<kDefaultTimeoutCycles, kNumSGEs, false>(
                peer, dst, sges);
        }
        coop.sync();
    }

    template <int kNumSGEs, typename Coop, bool kDoCommit = true>
    __aicore__ inline void Put(
            const Coop& coop,
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        Write(coop, peer, dst, sges);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __aicore__ inline void Put(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        Write(peer, dst, sges);
        if constexpr (kDoCommit) {
            Commit();
        }
    }

    template <typename Coop>
    __aicore__ inline void Commit(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->Commit();
        }
        coop.sync();
    }

    __aicore__ inline void Commit()
    {
        const auto total_writes = *num_writes;
        if (total_writes == 0) {
            return;
        }

        HCOMM_JETTY_ASSERT(total_writes * static_cast<int>(kNumSimtWriteWQEBBs) <= static_cast<int>(jetty_info->sq_depth));

        const auto old_head = static_cast<uint32_t>(jetty_info->packed_head);
        const auto new_head = old_head + total_writes * kNumSimtWriteWQEBBs;
        const auto new_num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32) + 1;
        jetty_info->packed_head = static_cast<uint64_t>(new_head) |
            (static_cast<uint64_t>(new_num_completions) << 32);

        asc_threadfence();
        auto* dwqe_words = reinterpret_cast<__gm__ ulonglong2*>(jetty_info->dwqe_addr);
        asc_stwt(&dwqe_words[0], make_ulonglong2(dwqe_stage[24], dwqe_stage[25]));
        asc_stwt(&dwqe_words[1], make_ulonglong2(dwqe_stage[26], dwqe_stage[27]));
        asc_stwt(&dwqe_words[2], make_ulonglong2(dwqe_stage[28], dwqe_stage[29]));
        asc_stwt(&dwqe_words[3], make_ulonglong2(dwqe_stage[30], dwqe_stage[31]));
        asc_stwt(&dwqe_words[4], make_ulonglong2(jetty_info->packed_head, 0ULL));
        asc_stwt(&dwqe_words[5], make_ulonglong2(0ULL, 0ULL));
        asc_stwt(&dwqe_words[6], make_ulonglong2(0ULL, 0ULL));
        asc_stwt(&dwqe_words[7], make_ulonglong2(static_cast<uint64_t>(new_head), 0ULL));
        asc_dcci_single(jetty_info->dwqe_addr);
        asc_dcci_single(
            reinterpret_cast<__gm__ uint8_t*>(jetty_info->dwqe_addr) + kNumWQEBBBytes);
    }

    template <int64_t kNumTimeoutCycles>
    __aicore__ inline void Drain()
    {
        PollCq<kNumTimeoutCycles>(
            static_cast<uint32_t>(
                ReadGmByPassDCache(
                    reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr)) >> 32));
    }

    template <int64_t kNumTimeoutCycles, typename Coop>
    __aicore__ inline void Drain(const Coop& coop)
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            Drain<kNumTimeoutCycles>();
        }
        coop.sync();
    }

private:
    template <int64_t kNumTimeoutCycles>
    __aicore__ inline void PollCq(const uint32_t expected_tail)
    {
        static constexpr uint32_t kNumCQDoorbellBits = 24;
        static constexpr uint32_t kCQEOwnerBit = 2;

        auto current_tail = *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr);
        while (current_tail != expected_tail) {
            const auto cqe = reinterpret_cast<__gm__ uint32_t*>(
                jetty_info->cq_base_addr +
                static_cast<uint64_t>(jetty_info->num_cqe_bytes) *
                    (current_tail & (jetty_info->cq_depth - 1)));
            const auto tail_phase = (current_tail / jetty_info->cq_depth) & 1;
            const auto start_cycle = static_cast<uint64_t>(__clock64());
            uint32_t cqe_header = 0;
            while (true) {
                cqe_header = *cqe;
                const auto cqe_owner = (cqe_header >> kCQEOwnerBit) & 1;
                if (cqe_owner != tail_phase) {
                    break;
                }

                if (static_cast<uint64_t>(__clock64()) - start_cycle >= kNumTimeoutCycles) {
                    printf("Hcomm Jetty CQ poll timeout, current tail: %u, expected tail: %u, cqe: 0x%x\n",
                           current_tail, expected_tail, cqe_header);
                    trap();
                }
                Nop<300>();
            }

            const auto substatus = static_cast<uint8_t>(cqe_header >> 16);
            const auto status = static_cast<uint8_t>(cqe_header >> 24);
            if (status != 0 or substatus != 0) {
                printf("Hcomm Jetty CQE failed, current tail: %u, status: %u, substatus: %u\n",
                       current_tail, status, substatus);
                trap();
            }
            ++current_tail;
        }

        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr) = current_tail;
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_doorbell_addr) =
            current_tail & ((1U << kNumCQDoorbellBits) - 1);
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr) = current_tail;
    }
};

#else

#include <simt_api/cooperative_groups.h>

namespace simt {

struct HcommCoopThread {
    __simt_callee__ inline void sync() const {}
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

struct HcommPeer {
    __ubuf__ HcommPeerInfo* peer_info = nullptr;

    __simt_callee__ inline HcommPeer(
            __ubuf__ HcommPeerInfo* peer_info,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        peer_info(peer_info)
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
    static constexpr uint32_t kNumSimtWriteWQEBBs = 4;
    static constexpr uint32_t kNumSimtWQEWords = kNumSimtWriteWQEBBs * (kNumWQEBBBytes / sizeof(uint64_t));
    static constexpr uint32_t kNumMaxSQEBytes = kNumSimtWriteWQEBBs * kNumWQEBBBytes;
    static constexpr int64_t kDefaultTimeoutCycles = 1000000LL;

    __ubuf__ HcommJettyInfo* jetty_info = nullptr;
    __ubuf__ int* write_counter = nullptr;
    __ubuf__ int* num_writes = nullptr;
    __ubuf__ uint8_t* ub_sqe = nullptr;
    __ubuf__ uint64_t* dwqe_stage = nullptr;

    __simt_callee__ inline HcommJetty() = default;

    __simt_callee__ inline HcommJetty(
            __ubuf__ HcommJettyInfo* jetty_info,
            __ubuf__ int* write_counter,
            __ubuf__ int* num_writes,
            __ubuf__ uint8_t* ub_sqe,
            __ubuf__ uint64_t* dwqe_stage,
            __gm__ void* jetty_ptrs, const int jetty_idx):
        HcommJetty()
    {
        Init(jetty_info, write_counter, num_writes, ub_sqe, dwqe_stage, jetty_ptrs, jetty_idx);
    }

    __simt_callee__ inline void Init(
            __ubuf__ HcommJettyInfo* jetty_info,
            __ubuf__ int* write_counter,
            __ubuf__ int* num_writes,
            __ubuf__ uint8_t* ub_sqe,
            __ubuf__ uint64_t* dwqe_stage,
            __gm__ void* jetty_ptrs, const int jetty_idx)
    {
        this->jetty_info = jetty_info;
        this->write_counter = write_counter;
        this->num_writes = num_writes;
        this->ub_sqe = ub_sqe;
        this->dwqe_stage = dwqe_stage;
        constexpr uint32_t kDwqeDbOffset = 0x80U;
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = *(jetty_table + jetty_idx);
        HCOMM_JETTY_ASSERT(jetty_value != 0);
        const auto jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
        const auto sq_context = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        const auto cq_context = jetty->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
        jetty_info->sq_base_addr = sq_context.contextInfo.ubJfs.sqVa;
        jetty_info->sq_head_addr = sq_context.contextInfo.ubJfs.headAddr;
        jetty_info->completion_tail_addr = sq_context.contextInfo.ubJfs.tailAddr;
        jetty_info->sq_doorbell_addr = sq_context.contextInfo.ubJfs.dbVa;
        jetty_info->dwqe_addr = sq_context.contextInfo.ubJfs.dbVa - kDwqeDbOffset;
        jetty_info->sq_depth = sq_context.contextInfo.ubJfs.sqDepth;
        jetty_info->num_wqebb_bytes = sq_context.contextInfo.ubJfs.wqeSize;
        jetty_info->cq_base_addr = cq_context.contextInfo.ubJfc.scqVa;
        jetty_info->cq_tail_addr = cq_context.contextInfo.ubJfc.tailAddr;
        jetty_info->cq_doorbell_addr = cq_context.contextInfo.ubJfc.dbVa;
        jetty_info->num_cqe_bytes = cq_context.contextInfo.ubJfc.cqeSize;
        jetty_info->cq_depth = cq_context.contextInfo.ubJfc.cqDepth;
        jetty_info->packed_head = *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr);
        HCOMM_JETTY_ASSERT(jetty_info->num_wqebb_bytes == kNumWQEBBBytes);
        HCOMM_JETTY_ASSERT(jetty_detail::IsPowerOf2(jetty_info->sq_depth));
        HCOMM_JETTY_ASSERT(jetty_info->sq_depth >= kNumSimtWriteWQEBBs);
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
            PollCq<kNumTimeoutCycles>(
                jetty_detail::Min(completion_tail + kNumCQEsPerPoll, num_completions));
        }
    }

    template <int kNumSGEs>
    __simt_callee__ inline void BuildWriteSqe(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs],
            const uint32_t current_head)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEPairsPerSQE * 2, "SIMT jetty SGE count exceeds the SQE layout");

        const auto peer_info_words = reinterpret_cast<__ubuf__ uint64_t*>(peer.peer_info);
        auto* sqe_words = reinterpret_cast<__ubuf__ uint64_t*>(ub_sqe);
        for (uint32_t i = 0; i < kNumSimtWQEWords; ++i) {
            sqe_words[i] = 0;
            dwqe_stage[i] = 0;
        }

        const auto sq_wqebb_idx = current_head % jetty_info->sq_depth;
        const auto owner_bit = static_cast<uint64_t>((current_head & jetty_info->sq_depth) == 0) << 31;

        sqe_words[0] = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        sqe_words[1] = peer_info_words[0] | (static_cast<uint64_t>(kNumSGEs) << 24);
        sqe_words[2] = peer_info_words[1];
        sqe_words[3] = peer_info_words[2];
        sqe_words[4] = peer_info_words[3];
        sqe_words[5] = reinterpret_cast<uint64_t>(dst);

        constexpr uint32_t kSgeWordBase = 6;
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            const auto& sge = sges[sge_idx];
            const uint32_t word_idx = kSgeWordBase + static_cast<uint32_t>(sge_idx) * 2U;
            const uint64_t len = (sge.addr == 0 || sge.len <= 0) ? 0ULL : static_cast<uint64_t>(sge.len);
            const uint64_t addr = (sge.addr == 0 || sge.len <= 0) ? 0ULL : sge.addr;
            sqe_words[word_idx] = len;
            sqe_words[word_idx + 1U] = addr;
        }

        for (uint32_t i = 0; i < kNumSimtWQEWords; ++i) {
            dwqe_stage[i] = sqe_words[i];
        }
    }

    template <int kNumSGEs>
    __simt_callee__ inline void CopySqeToSq(const uint32_t current_head) const
    {
        const auto sq_idx = current_head & (jetty_info->sq_depth - 1);
        const auto num_first_wqebbs =
            jetty_detail::Min<uint32_t>(kNumSimtWriteWQEBBs, jetty_info->sq_depth - sq_idx);
        constexpr uint32_t kNumWordsPerWqebb = kNumWQEBBBytes / sizeof(uint64_t);
        const auto* sqe_words = reinterpret_cast<const __ubuf__ uint64_t*>(ub_sqe);
        auto* sq_words = reinterpret_cast<__gm__ ulonglong2*>(
            jetty_info->sq_base_addr + static_cast<uint64_t>(jetty_info->num_wqebb_bytes) * sq_idx);

        for (uint32_t i = 0; i < num_first_wqebbs * (kNumWQEBBBytes / sizeof(ulonglong2)); ++i) {
            const uint32_t word_idx = i * 2U;
            sq_words[i] = make_ulonglong2(sqe_words[word_idx], sqe_words[word_idx + 1U]);
        }
        if (num_first_wqebbs < kNumSimtWriteWQEBBs) {
            auto* sq_words_head = reinterpret_cast<__gm__ ulonglong2*>(jetty_info->sq_base_addr);
            const uint32_t offset_words = num_first_wqebbs * (kNumWQEBBBytes / sizeof(ulonglong2));
            const uint32_t tail_words = (kNumSimtWriteWQEBBs - num_first_wqebbs) * (kNumWQEBBBytes / sizeof(ulonglong2));
            for (uint32_t i = 0; i < tail_words; ++i) {
                const uint32_t word_idx = offset_words + i * 2U;
                sq_words_head[i] = make_ulonglong2(sqe_words[word_idx], sqe_words[word_idx + 1U]);
            }
        }
    }

    __simt_callee__ inline void AdvanceSq(const uint32_t num_wqebbs)
    {
        const auto new_head = static_cast<uint32_t>(jetty_info->packed_head) + num_wqebbs;
        const auto new_num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32) + 1;
        jetty_info->packed_head = static_cast<uint64_t>(new_head) |
            (static_cast<uint64_t>(new_num_completions) << 32);
        *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr) = jetty_info->packed_head;
    }

    __simt_callee__ inline void RingDoorbell()
    {
        *reinterpret_cast<__gm__ uint32_t*>(jetty_info->sq_doorbell_addr) = static_cast<uint32_t>(jetty_info->packed_head);
    }

    template <int64_t kNumTimeoutCycles, int kNumSGEs, bool kDoCommit>
    __simt_callee__ inline void PostWrite(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs])
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        HCOMM_JETTY_ASSERT(*num_writes > 0);
        HCOMM_JETTY_ASSERT((*num_writes) * static_cast<int>(kNumSimtWriteWQEBBs) <= static_cast<int>(jetty_info->sq_depth));

        PollCqWhenSqOverflow<kNumTimeoutCycles>();
        const int write_idx = (*write_counter)++;
        HCOMM_JETTY_ASSERT(write_idx >= 0);
        HCOMM_JETTY_ASSERT(write_idx < *num_writes);

        const auto current_head = static_cast<uint32_t>(jetty_info->packed_head) +
            static_cast<uint32_t>(write_idx) * kNumSimtWriteWQEBBs;

        BuildWriteSqe<kNumSGEs>(peer, dst, sges, current_head);
        CopySqeToSq<kNumSGEs>(current_head);

        if constexpr (kDoCommit) {
            AdvanceSq(kNumSimtWriteWQEBBs);
            RingDoorbell();
        }
    }

    template <int kNumSGEs>
    __simt_callee__ inline void Write(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        const_cast<HcommJetty*>(this)->template PostWrite<kDefaultTimeoutCycles, kNumSGEs, false>(peer, dst, sges);
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ inline void Write(
            const Coop& coop,
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->template PostWrite<kDefaultTimeoutCycles, kNumSGEs, false>(
                peer, dst, sges);
        }
        coop.sync();
    }

    template <int kNumSGEs, typename Coop, bool kDoCommit = true>
    __simt_callee__ inline void Put(
            const Coop& coop,
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        Write(coop, peer, dst, sges);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ inline void Put(
            const HcommPeer& peer,
            __gm__ void* dst,
            const HcommSge (&sges)[kNumSGEs]) const
    {
        Write(peer, dst, sges);
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
        const auto total_writes = *num_writes;
        if (total_writes == 0) {
            return;
        }

        HCOMM_JETTY_ASSERT(total_writes * static_cast<int>(kNumSimtWriteWQEBBs) <= static_cast<int>(jetty_info->sq_depth));

        const auto old_head = static_cast<uint32_t>(jetty_info->packed_head);
        const auto new_head = old_head + total_writes * kNumSimtWriteWQEBBs;
        const auto new_num_completions = static_cast<uint32_t>(jetty_info->packed_head >> 32) + 1;
        jetty_info->packed_head = static_cast<uint64_t>(new_head) |
            (static_cast<uint64_t>(new_num_completions) << 32);
        *reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr) = jetty_info->packed_head;

        auto* dwqe_words = reinterpret_cast<__gm__ ulonglong2*>(jetty_info->dwqe_addr);
        dwqe_words[0] = make_ulonglong2(dwqe_stage[24], dwqe_stage[25]);
        dwqe_words[1] = make_ulonglong2(dwqe_stage[26], dwqe_stage[27]);
        dwqe_words[2] = make_ulonglong2(dwqe_stage[28], dwqe_stage[29]);
        dwqe_words[3] = make_ulonglong2(dwqe_stage[30], dwqe_stage[31]);
        dwqe_words[4] = make_ulonglong2(jetty_info->packed_head, 0ULL);
        dwqe_words[5] = make_ulonglong2(0ULL, 0ULL);
        dwqe_words[6] = make_ulonglong2(0ULL, 0ULL);
        dwqe_words[7] = make_ulonglong2(static_cast<uint64_t>(new_head), 0ULL);
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
                jetty_info->cq_base_addr +
                static_cast<uint64_t>(jetty_info->num_cqe_bytes) *
                    (current_tail & (jetty_info->cq_depth - 1)));
            const auto tail_phase = (current_tail / jetty_info->cq_depth) & 1;
            while (true) {
                const auto cqe_header = *cqe;
                const auto cqe_owner = (cqe_header >> kCQEOwnerBit) & 1;
                if (cqe_owner != tail_phase) {
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

#undef HCOMM_JETTY_ASSERT

#endif

} // namespace AscendC

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
