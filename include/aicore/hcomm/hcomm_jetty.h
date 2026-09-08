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

#include <cstddef>
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

#define HCOMM_JETTY_CHECK_OR_RETURN(cond) \
    do {                                  \
        if (!(cond)) {                    \
            return;                       \
        }                                 \
    } while (0)

namespace AscendC {

constexpr int kNumMaxSGEPairsPerSQE = 6;
constexpr int kNumWQEBBBytes = 64;

constexpr uint64_t kNopSQEHeaderTemplate = 0x0000110000000000ULL;
constexpr uint64_t kWriteSQEHeaderTemplate = 0x0000030030010000ULL;
constexpr uint64_t kWriteSQEFinalFlags = (1ULL << 18) | (1ULL << 19) | (1ULL << 21);

namespace jetty_detail {
template <typename dtype_t>
constexpr __aicore__ inline bool IsPowerOf2(dtype_t value)
{
    return value > 0 and (value & (value - 1)) == 0;
}

#if defined(USE_SIMT)
// ChannelEntity is shared with host/AIV code, so its pointer members do not carry a SIMT
// address-space qualifier. Load those ABI fields as integers before forming __gm__ pointers;
// directly following channel->sqContextAddr/cqContextAddr can turn the loaded value into a
// private VF pointer and surface as an RTS stack-address fault.
template <typename T>
__simt_callee__ __forceinline__ T LoadChannelField(const uint64_t channel, const uint32_t offset)
{
    const auto field = reinterpret_cast<__gm__ T*>(reinterpret_cast<__gm__ uint8_t*>(channel) + offset);
    return *field;
}

__simt_callee__ __forceinline__ __gm__ SqContext* GetSqContexts(const uint64_t channel)
{
    const uint64_t address = LoadChannelField<uint64_t>(channel, offsetof(ChannelEntity, sqContextAddr));
    return reinterpret_cast<__gm__ SqContext*>(address);
}

__simt_callee__ __forceinline__ __gm__ CqContext* GetCqContexts(const uint64_t channel)
{
    const uint64_t address = LoadChannelField<uint64_t>(channel, offsetof(ChannelEntity, cqContextAddr));
    return reinterpret_cast<__gm__ CqContext*>(address);
}

__simt_callee__ __forceinline__ __gm__ RegedBufferEntity* GetRemoteBuffers(const uint64_t channel)
{
    const uint64_t address = LoadChannelField<uint64_t>(channel, offsetof(ChannelEntity, remoteBufferAddr));
    return reinterpret_cast<__gm__ RegedBufferEntity*>(address);
}
#endif

} // namespace jetty_detail

#if defined(USE_SIMT)

namespace simt {

struct HcommCoopThread {
    __simt_callee__ __forceinline__ void sync() const {}
    __simt_callee__ __forceinline__ int size() const { return 1; }
    __simt_callee__ __forceinline__ int thread_rank() const { return 0; }
};

struct HcommCoopWarp {
    cooperative_groups::thread_block_tile<32, cooperative_groups::thread_block> group_;

    __simt_callee__ __forceinline__ HcommCoopWarp()
        : group_(cooperative_groups::tiled_partition<32>(cooperative_groups::this_thread_block()))
    {}

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

namespace jetty {

struct alignas(8) HcommSge {
    uint64_t addr = 0;
    int len = 0;
};
static_assert(sizeof(HcommSge) == 16, "SIMT HcommSge size mismatch");
static_assert(alignof(HcommSge) == 8, "SIMT HcommSge alignment mismatch");

struct HcommPeer {
    uint64_t channel = 0;

    __simt_callee__ __forceinline__ HcommPeer() = default;

    __simt_callee__ __forceinline__ HcommPeer(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        Init(jetty_ptrs, jetty_idx);
    }

    __simt_callee__ __forceinline__ void Init(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = asc_ldcg(jetty_table + jetty_idx);
        HCOMM_JETTY_CHECK_OR_RETURN(jetty_value != 0);
        channel = jetty_value;
        const uint32_t remote_buffer_num =
            jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, remoteBufferNum));
        const uint32_t sq_num = jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, sqNum));
        HCOMM_JETTY_CHECK_OR_RETURN(remote_buffer_num > 0 && sq_num > 0);
    }
};
static_assert(sizeof(HcommPeer) == sizeof(uint64_t), "SIMT peer handle must only contain a GM channel pointer");

struct HcommJetty {
    static constexpr int kWarpSize = 32;
    static constexpr int kNumMaxSGEsPerWrite = 2 * kNumMaxSGEPairsPerSQE;
    static constexpr int kNumWQEBBsPerWrite =
        (sizeof(HcommUrmaSqeCtx) + kNumMaxSGEsPerWrite * sizeof(HcommUrmaSgeCtx) + kNumWQEBBBytes - 1) / kNumWQEBBBytes;
    static constexpr int kNumWQEBBLanes = kNumWQEBBBytes / sizeof(uint64_t);
    static constexpr int kNumWriteHeaderLanes = sizeof(HcommUrmaSqeCtx) / sizeof(uint64_t);
    static constexpr uint32_t kNumSimtWriteWQEBBs = kNumWQEBBsPerWrite;
    static constexpr uint32_t kNumMaxSQEBytes = kNumWQEBBsPerWrite * kNumWQEBBBytes;
    static_assert(kNumWriteHeaderLanes == 6, "SIMT URMA SQE header must contain six 64-bit words");
    static_assert(kNumWQEBBsPerWrite == 4, "SIMT URMA write reservation must contain four WQEBBs");
    static_assert(kNumMaxSGEsPerWrite <= kWarpSize - kNumWriteHeaderLanes, "SIMT URMA SGE layout must fit in one warp");

    // One channel batch has one owner: all participating warps reserve through this GM head,
    // then the batch's group leader calls Commit after every writer has synchronized.
    uint64_t channel = 0;
    uint32_t batch_begin_head = 0;

    __simt_callee__ __forceinline__ HcommJetty() = default;

    __simt_callee__ __forceinline__ HcommJetty(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        Init(jetty_ptrs, jetty_idx);
    }

    __simt_callee__ __forceinline__ void Init(__gm__ void* jetty_ptrs, const int jetty_idx)
    {
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = asc_ldcg(jetty_table + jetty_idx);
        HCOMM_JETTY_CHECK_OR_RETURN(jetty_value != 0);
        channel = jetty_value;
        const uint32_t sq_num = jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, sqNum));
        const uint32_t cq_num = jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, cqNum));
        HCOMM_JETTY_CHECK_OR_RETURN(sq_num > 0 && cq_num > 0);
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        HCOMM_JETTY_CHECK_OR_RETURN(sq_context->contextInfo.ubJfs.wqeSize == kNumWQEBBBytes);
        HCOMM_JETTY_CHECK_OR_RETURN(jetty_detail::IsPowerOf2(sq_context->contextInfo.ubJfs.sqDepth));
        HCOMM_JETTY_CHECK_OR_RETURN(sq_context->contextInfo.ubJfs.sqDepth >= kNumWQEBBsPerWrite);
        const auto head_addr = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.headAddr);
        batch_begin_head = static_cast<uint32_t>(asc_ldcg(head_addr));
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteWarp(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");

        const int lane_idx = static_cast<int>(threadIdx.x) % kWarpSize;
        uint64_t target_len = 0;
#pragma unroll
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            const uint64_t candidate_addr = __shfl(sge.addr, sge_idx, kWarpSize);
            const int candidate_len = __shfl(sge.len, sge_idx, kWarpSize);
            if (candidate_addr != 0 && candidate_len > 0) {
                target_len += static_cast<uint64_t>(candidate_len);
            }
        }
        uint32_t remote_buffer_idx = kInvalidRemoteBuffer;
        if (lane_idx == 0) {
            remote_buffer_idx = FindRemoteBuffer(peer.channel, dst, target_len);
        }
        remote_buffer_idx = __shfl(remote_buffer_idx, 0, kWarpSize);
        HCOMM_JETTY_CHECK_OR_RETURN(remote_buffer_idx != kInvalidRemoteBuffer);

        uint32_t current_head = kInvalidHead;
        if (lane_idx == 0) {
            current_head = ReserveWrite();
        }
        current_head = __shfl(current_head, 0, kWarpSize);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);

        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        const uint32_t sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
        const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
        const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0) << 31;
        const int raw_sge_lane = (lane_idx - kNumWriteHeaderLanes) / 2;
        const int sge_lane =
            raw_sge_lane < 0 ? 0 : (raw_sge_lane >= kNumMaxSGEsPerWrite ? kNumMaxSGEsPerWrite - 1 : raw_sge_lane);
        const uint64_t shuffled_sge_addr = __shfl(sge.addr, sge_lane, kWarpSize);
        const int shuffled_sge_len = __shfl(sge.len, sge_lane, kWarpSize);

        uint64_t sqe_word = 0;
        if (lane_idx == 0) {
            sqe_word = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        } else if (lane_idx < kNumWriteHeaderLanes) {
            sqe_word = PeerHeaderWord<kNumSGEs>(peer, remote_buffer_idx, lane_idx, dst);
        } else if (lane_idx < kNumWriteHeaderLanes + kNumSGEs * 2) {
            const bool is_len_word = ((lane_idx - kNumWriteHeaderLanes) % 2) == 0;
            const bool valid_sge = shuffled_sge_addr != 0 && shuffled_sge_len > 0;
            sqe_word = valid_sge ? (is_len_word ? static_cast<uint64_t>(shuffled_sge_len) : shuffled_sge_addr) : 0;
        } else if (lane_idx % kNumWQEBBLanes == 0) {
            // Unused BBs in the fixed four-BB reservation are real NOP WQEs.
            sqe_word = kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        }

        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.sqVa);
        asc_stwt(
            sq_words + static_cast<uint64_t>(sq_wqebb_idx) * kNumWQEBBLanes + lane_idx % kNumWQEBBLanes,
            sqe_word);
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalar(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs])
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");
        uint64_t target_len = 0;
#pragma unroll
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            if (sges[sge_idx].addr != 0 && sges[sge_idx].len > 0) {
                target_len += static_cast<uint64_t>(sges[sge_idx].len);
            }
        }
        const uint32_t remote_buffer_idx = FindRemoteBuffer(peer.channel, dst, target_len);
        HCOMM_JETTY_CHECK_OR_RETURN(remote_buffer_idx != kInvalidRemoteBuffer);
        const uint32_t current_head = ReserveWrite();
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        const uint32_t sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.sqVa);
        for (int lane_idx = 0; lane_idx < kWarpSize; ++lane_idx) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0) << 31;
            uint64_t sqe_word = 0;
            if (lane_idx == 0) {
                sqe_word = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
            } else if (lane_idx < kNumWriteHeaderLanes) {
                sqe_word = PeerHeaderWord<kNumSGEs>(peer, remote_buffer_idx, lane_idx, dst);
            } else if (lane_idx < kNumWriteHeaderLanes + kNumSGEs * 2) {
                const int sge_idx = (lane_idx - kNumWriteHeaderLanes) / 2;
                const auto& scalar_sge = sges[sge_idx];
                const bool is_len_word = ((lane_idx - kNumWriteHeaderLanes) % 2) == 0;
                const bool valid_sge = scalar_sge.addr != 0 && scalar_sge.len > 0;
                sqe_word = valid_sge ? (is_len_word ? static_cast<uint64_t>(scalar_sge.len) : scalar_sge.addr) : 0;
            } else if (lane_idx % kNumWQEBBLanes == 0) {
                sqe_word = kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
            }
            asc_stwt(
                sq_words + static_cast<uint64_t>(sq_wqebb_idx) * kNumWQEBBLanes + lane_idx % kNumWQEBBLanes,
                sqe_word);
        }
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalar(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& first_sge)
    {
        HcommSge sges[kNumSGEs] = {};
        sges[0] = first_sge;
        PostWriteScalar<kNumSGEs>(peer, dst, sges);
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void Write(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sge);
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void Write(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sges);
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ __forceinline__ void Write(
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
        asc_threadfence();
        coop.sync();
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ __forceinline__ void Write(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->template PostWriteScalar<kNumSGEs>(peer, dst, sges);
        }
        asc_threadfence();
        coop.sync();
    }

    template <int kNumSGEs, bool kDoCommit = true, typename Coop>
    __simt_callee__ __forceinline__ void Put(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        Write<kNumSGEs>(coop, peer, dst, sge);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true, typename Coop>
    __simt_callee__ __forceinline__ void Put(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        Write<kNumSGEs>(coop, peer, dst, sges);
        if constexpr (kDoCommit) {
            Commit(coop);
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ __forceinline__ void Put(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        Write<kNumSGEs>(peer, dst, sge);
        if constexpr (kDoCommit) {
            const_cast<HcommJetty*>(this)->Commit();
        }
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ __forceinline__ void Put(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        Write<kNumSGEs>(peer, dst, sges);
        if constexpr (kDoCommit) {
            const_cast<HcommJetty*>(this)->Commit();
        }
    }

    template <typename Coop>
    __simt_callee__ __forceinline__ void Commit(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->Commit();
        }
        coop.sync();
    }

    __simt_callee__ __forceinline__ void Commit()
    {
        constexpr uint64_t kCompletionDelta = 1ULL << 32U;
        constexpr uint32_t kDwqeDbOffset = 0x80U;
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* head_addr = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.headAddr);
        const uint64_t packed_head = asc_ldcg(head_addr);
        const uint32_t new_head = static_cast<uint32_t>(packed_head);
        const uint32_t batch_wqebbs = new_head - batch_begin_head;
        if (batch_wqebbs == 0U) {
            return;
        }
        const uint32_t sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        HCOMM_JETTY_CHECK_OR_RETURN(batch_wqebbs <= sq_depth && batch_wqebbs % kNumWQEBBsPerWrite == 0U);

        const uint32_t last_write_head = new_head - kNumWQEBBsPerWrite;
        const uint32_t last_write_idx = last_write_head & (sq_depth - 1U);
        auto* last_write_header = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.sqVa) +
                                  static_cast<uint64_t>(last_write_idx) * kNumWQEBBLanes;
        const auto owner_bit = static_cast<uint64_t>((last_write_head & sq_depth) == 0U) << 31;
        const auto final_header =
            kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(new_head & 0xFFFFU) | kWriteSQEFinalFlags;
        asc_stwt(last_write_header, final_header);

        asc_threadfence();
        (void)asc_atomic_add(head_addr, kCompletionDelta);
        auto* dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(sq_context->contextInfo.ubJfs.dbVa - kDwqeDbOffset);
        auto* dwqe_words = reinterpret_cast<__gm__ ulonglong2*>(dwqe_addr);
#pragma unroll
        for (uint32_t pair = 0; pair < 2U * kNumWQEBBBytes / sizeof(ulonglong2); ++pair) {
            const uint32_t first_word = pair * 2U;
            const uint32_t first_bb = first_word / kNumWQEBBLanes;
            const uint32_t word_in_bb = first_word % kNumWQEBBLanes;
            const uint32_t source_idx = (last_write_head + first_bb) & (sq_depth - 1U);
            const auto source_words = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.sqVa) +
                                      static_cast<uint64_t>(source_idx) * kNumWQEBBLanes + word_in_bb;
            const uint64_t first_value = first_word == 0U ? final_header : asc_ldcg(source_words);
            const uint64_t second_value = asc_ldcg(source_words + 1);
            asc_stwt(&dwqe_words[pair], make_ulonglong2(first_value, second_value));
        }
        asc_dcci_single(dwqe_addr);
        asc_dcci_single(dwqe_addr + kNumWQEBBBytes);
        batch_begin_head = new_head;
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ __forceinline__ void Drain() const
    {
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        const auto head_addr = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.headAddr);
        PollCq<kNumTimeoutCycles>(static_cast<uint32_t>(asc_ldcg(head_addr) >> 32U));
    }

    template <int64_t kNumTimeoutCycles, typename Coop>
    __simt_callee__ __forceinline__ void Drain(const Coop& coop) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            Drain<kNumTimeoutCycles>();
        }
        coop.sync();
    }

private:
    static constexpr uint32_t kInvalidHead = 0xFFFFFFFFU;
    static constexpr uint32_t kInvalidRemoteBuffer = 0xFFFFFFFFU;

    __simt_callee__ __forceinline__ static uint32_t FindRemoteBuffer(
        const uint64_t peer_channel, __gm__ void* dst, const uint64_t len)
    {
        const uint64_t target_addr = reinterpret_cast<uint64_t>(dst);
        const uint32_t remote_buffer_num =
            jetty_detail::LoadChannelField<uint32_t>(peer_channel, offsetof(ChannelEntity, remoteBufferNum));
        const auto remote_buffers = jetty_detail::GetRemoteBuffers(peer_channel);
        for (uint32_t buffer_idx = 0; buffer_idx < remote_buffer_num; ++buffer_idx) {
            const uint64_t base_addr = remote_buffers[buffer_idx].bufferInfo.rma.addr;
            const uint64_t buffer_size = remote_buffers[buffer_idx].bufferInfo.rma.size;
            if (target_addr < base_addr) {
                continue;
            }
            const uint64_t offset = target_addr - base_addr;
            if (offset <= buffer_size && len <= buffer_size - offset) {
                return buffer_idx;
            }
        }
        return kInvalidRemoteBuffer;
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ static uint64_t PeerHeaderWord(
        const HcommPeer& peer, const uint32_t remote_buffer_idx, const int lane_idx, __gm__ void* dst)
    {
        const auto peer_channel = peer.channel;
        const auto sq_context = jetty_detail::GetSqContexts(peer_channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        const auto remote_buffer = jetty_detail::GetRemoteBuffers(peer_channel) + remote_buffer_idx;
        if (lane_idx == 1) {
            return static_cast<uint64_t>(sq_context->contextInfo.ubJfs.tpID & 0xFFFFFFU) |
                   (static_cast<uint64_t>(kNumSGEs) << 24U) |
                   (static_cast<uint64_t>(
                        remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU)
                    << 32U);
        }
        if (lane_idx == 2 || lane_idx == 3) {
            uint64_t remote_eid_word = 0;
            const int first_byte = (lane_idx - 2) * 8;
#pragma unroll
            for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
                remote_eid_word |= static_cast<uint64_t>(sq_context->contextInfo.ubJfs.remoteEID[first_byte + byte_idx])
                                   << (byte_idx * 8);
            }
            return remote_eid_word;
        }
        if (lane_idx == 4) {
            return remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        }
        return reinterpret_cast<uint64_t>(dst);
    }

    __simt_callee__ __forceinline__ uint32_t ReserveWrite() const
    {
        static constexpr uint32_t kMaxReservationRetries = 1024U;
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* head_addr = reinterpret_cast<__gm__ uint64_t*>(sq_context->contextInfo.ubJfs.headAddr);
        const auto tail_addr = reinterpret_cast<__gm__ uint32_t*>(sq_context->contextInfo.ubJfs.tailAddr);
        const uint32_t sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        for (uint32_t retry = 0; retry < kMaxReservationRetries; ++retry) {
            const uint64_t packed_head = asc_ldcg(head_addr);
            const uint32_t current_head = static_cast<uint32_t>(packed_head);
            const uint32_t used_wqebbs = current_head - asc_ldcg(tail_addr);
            if (used_wqebbs > sq_depth || kNumWQEBBsPerWrite > sq_depth - used_wqebbs) {
                return kInvalidHead;
            }
            const uint64_t next_packed_head =
                (packed_head & 0xFFFFFFFF00000000ULL) | static_cast<uint64_t>(current_head + kNumWQEBBsPerWrite);
            if (asc_atomic_cas(head_addr, packed_head, next_packed_head) == packed_head) {
                return current_head;
            }
        }
        return kInvalidHead;
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ __forceinline__ void PollCq(const uint32_t expected_tail) const
    {
        static_assert(kNumTimeoutCycles > 0, "SIMT jetty CQ timeout must be positive");
        static constexpr uint32_t kNumCQDoorbellBits = 24;
        static constexpr uint32_t kCQEOwnerBit = 2;
        const auto cq_context = jetty_detail::GetCqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* cq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(cq_context->contextInfo.ubJfc.tailAddr);
        auto* sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(sq_context->contextInfo.ubJfs.tailAddr);
        uint32_t current_tail = *cq_tail_addr;
        if (current_tail == expected_tail) {
            return;
        }
        uint32_t sq_tail = *sq_tail_addr;
        const uint32_t cq_depth = cq_context->contextInfo.ubJfc.cqDepth;
        const uint32_t sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        while (current_tail != expected_tail) {
            const auto cqe = reinterpret_cast<__gm__ uint32_t*>(
                cq_context->contextInfo.ubJfc.scqVa +
                static_cast<uint64_t>(cq_context->contextInfo.ubJfc.cqeSize) * (current_tail & (cq_depth - 1U)));
            const uint32_t tail_phase = (current_tail / cq_depth) & 1U;
            int64_t retry = 0;
            uint32_t cqe_header = *cqe;
            while (((cqe_header >> kCQEOwnerBit) & 1U) == tail_phase && retry < kNumTimeoutCycles) {
                asc_threadfence();
                asc_dcci_single(cqe);
                cqe_header = *cqe;
                ++retry;
            }
            HCOMM_JETTY_CHECK_OR_RETURN(retry < kNumTimeoutCycles);
            const auto substatus = static_cast<uint8_t>(cqe_header >> 16);
            const auto status = static_cast<uint8_t>(cqe_header >> 24);
            HCOMM_JETTY_CHECK_OR_RETURN(status == 0 && substatus == 0);
            const uint32_t sq_tail_slot = sq_tail & (sq_depth - 1U);
            const uint32_t completed_slot = cqe[1] & (sq_depth - 1U);
            sq_tail += ((completed_slot + sq_depth - sq_tail_slot) & (sq_depth - 1U)) + 1U;
            ++current_tail;
        }
        *cq_tail_addr = current_tail;
        *reinterpret_cast<__gm__ uint32_t*>(cq_context->contextInfo.ubJfc.dbVa) =
            current_tail & ((1U << kNumCQDoorbellBits) - 1U);
        *sq_tail_addr = sq_tail;
    }
};
static_assert(sizeof(HcommJetty) == 16, "SIMT jetty handle must not contain workspace pointers");

} // namespace jetty
} // namespace simt

#endif

#undef HCOMM_JETTY_CHECK_OR_RETURN

} // namespace AscendC

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
