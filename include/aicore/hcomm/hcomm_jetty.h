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
constexpr uint64_t kNopSQEFinalFlags = (1ULL << 16) | kWriteSQEFinalFlags | (1ULL << 28) | (1ULL << 29);

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

struct alignas(8) HcommSge {
    uint64_t addr = 0;
    int len = 0;
};
static_assert(sizeof(HcommSge) == 16, "SIMT HcommSge size mismatch");
static_assert(alignof(HcommSge) == 8, "SIMT HcommSge alignment mismatch");

struct alignas(8) HcommPeerInfo {
    uint64_t tp_id_num_sges_rmt_token_id = 0;
    uint64_t rmt_eid[2] = {};
    uint64_t rmt_token_value_udf = 0;
    uint64_t rmt_base_addr = 0;
    uint64_t rmt_buffer_size = 0;
};
static_assert(sizeof(HcommPeerInfo) == 48, "SIMT peer metadata size mismatch");
static_assert(alignof(HcommPeerInfo) == 8, "SIMT peer metadata alignment mismatch");

struct alignas(8) HcommJettyInfo {
    uint64_t sq_base_addr = 0;
    uint64_t sq_head_addr = 0;
    uint64_t sq_tail_addr = 0;
    uint64_t sq_doorbell_addr = 0;
    uint64_t cq_base_addr = 0;
    uint64_t cq_tail_addr = 0;
    uint64_t cq_doorbell_addr = 0;
    uint64_t packed_head = 0;
    uint32_t sq_depth = 0;
    uint32_t cq_depth = 0;
    uint32_t cqe_bytes = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(HcommJettyInfo) == 80, "SIMT jetty metadata size mismatch");
static_assert(alignof(HcommJettyInfo) == 8, "SIMT jetty metadata alignment mismatch");

struct HcommPeer {
    uint64_t channel = 0;
    uint64_t peer_info_addr = 0;

    __simt_callee__ __forceinline__ HcommPeer() = default;

    __simt_callee__ __forceinline__ HcommPeer(
        __ubuf__ HcommPeerInfo* peer_info, __gm__ void* jetty_ptrs, const int jetty_idx)
    {
        Init(peer_info, jetty_ptrs, jetty_idx);
    }

    __simt_callee__ __forceinline__ HcommPeer(__ubuf__ HcommPeerInfo* peer_info, const uint64_t channel_addr)
    {
        Attach(peer_info, channel_addr);
    }

    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommPeerInfo* peer_info, __gm__ void* jetty_ptrs, const int jetty_idx)
    {
        const auto jetty_table = static_cast<__gm__ uint64_t*>(jetty_ptrs);
        const auto jetty_value = asc_ldcg(jetty_table + jetty_idx);
        HCOMM_JETTY_CHECK_OR_RETURN(jetty_value != 0);
        channel = jetty_value;
        const uint32_t remote_buffer_num =
            jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, remoteBufferNum));
        const uint32_t sq_num = jetty_detail::LoadChannelField<uint32_t>(channel, offsetof(ChannelEntity, sqNum));
        HCOMM_JETTY_CHECK_OR_RETURN(remote_buffer_num > 0 && sq_num > 0);
        peer_info_addr = reinterpret_cast<uint64_t>(peer_info);
        auto* info = GetInfo();
        const auto sq_context = jetty_detail::GetSqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        info->tp_id_num_sges_rmt_token_id = static_cast<uint64_t>(sq_context->contextInfo.ubJfs.tpID);
        info->rmt_eid[0] = 0;
        info->rmt_eid[1] = 0;
#pragma unroll
        for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
            info->rmt_eid[0] |= static_cast<uint64_t>(sq_context->contextInfo.ubJfs.remoteEID[byte_idx]) << (byte_idx * 8);
            info->rmt_eid[1] |=
                static_cast<uint64_t>(sq_context->contextInfo.ubJfs.remoteEID[byte_idx + 8]) << (byte_idx * 8);
        }
        const auto remote_buffers = jetty_detail::GetRemoteBuffers(channel);
        CacheRemoteBuffer(remote_buffers + remote_buffer_num - 1U);
    }

    __simt_callee__ __forceinline__ __ubuf__ HcommPeerInfo* GetInfo() const
    {
        return reinterpret_cast<__ubuf__ HcommPeerInfo*>(peer_info_addr);
    }

    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommPeerInfo* peer_info, const uint64_t channel_addr)
    {
        channel = channel_addr;
        peer_info_addr = reinterpret_cast<uint64_t>(peer_info);
    }

    __simt_callee__ __forceinline__ void CacheRemoteBuffer(const __gm__ RegedBufferEntity* remote_buffer) const
    {
        auto* info = GetInfo();
        info->tp_id_num_sges_rmt_token_id =
            (info->tp_id_num_sges_rmt_token_id & 0x0000000000FFFFFFULL) |
            (static_cast<uint64_t>(remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU) << 32U);
        info->rmt_token_value_udf = remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        info->rmt_base_addr = remote_buffer->bufferInfo.rma.addr;
        info->rmt_buffer_size = remote_buffer->bufferInfo.rma.size;
    }
};
static_assert(sizeof(HcommPeer) == 16, "SIMT peer handle layout mismatch");

struct HcommJetty {
    static constexpr int kWarpSize = 32;
    static constexpr int kNumMaxSGEsPerWrite = 2 * kNumMaxSGEPairsPerSQE;
    static constexpr int kNumWQEBBsPerWrite =
        (sizeof(HcommUrmaSqeCtx) + kNumMaxSGEsPerWrite * sizeof(HcommUrmaSgeCtx) + kNumWQEBBBytes - 1) / kNumWQEBBBytes;
    static constexpr int kNumWQEBBLanes = kNumWQEBBBytes / sizeof(uint64_t);
    static constexpr int kNumWriteHeaderLanes = sizeof(HcommUrmaSqeCtx) / sizeof(uint64_t);
    static constexpr int kNumDwqeWords = 2 * kNumWQEBBLanes;
    static constexpr uint32_t kNumSimtWriteWQEBBs = kNumWQEBBsPerWrite;
    static constexpr uint32_t kNumMaxSQEBytes = kNumWQEBBsPerWrite * kNumWQEBBBytes;
    static_assert(kNumWriteHeaderLanes == 6, "SIMT URMA SQE header must contain six 64-bit words");
    static_assert(kNumWQEBBsPerWrite == 4, "SIMT URMA write reservation must contain four WQEBBs");
    static_assert(kNumMaxSGEsPerWrite <= kWarpSize - kNumWriteHeaderLanes, "SIMT URMA SGE layout must fit in one warp");

    // One channel batch has one owner: all participating warps reserve through this GM head,
    // then the batch's group leader calls Commit after every writer has synchronized.
    uint64_t channel = 0;
    uint64_t jetty_info_addr = 0;
    uint64_t ub_sqe_addr = 0;
    uint32_t batch_begin_head = 0;
    uint32_t last_write_wqebbs = 0;
    uint32_t last_write_payload_wqebbs = 0;

    __simt_callee__ __forceinline__ HcommJetty() = default;

    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* jetty_info, __ubuf__ uint8_t* ub_sqe, __gm__ void* jetty_ptrs, const int jetty_idx)
    {
        Init(jetty_info, ub_sqe, jetty_ptrs, jetty_idx);
    }

    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* jetty_info, __ubuf__ uint8_t* ub_sqe, const uint64_t channel_addr)
    {
        Attach(jetty_info, ub_sqe, channel_addr);
    }

    __simt_callee__ __forceinline__ void Init(
        __ubuf__ HcommJettyInfo* jetty_info, __ubuf__ uint8_t* ub_sqe, __gm__ void* jetty_ptrs, const int jetty_idx)
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
        HCOMM_JETTY_CHECK_OR_RETURN(sq_context->contextInfo.ubJfs.sqDepth >= 2U);
        jetty_info_addr = reinterpret_cast<uint64_t>(jetty_info);
        ub_sqe_addr = reinterpret_cast<uint64_t>(ub_sqe);
        auto* info = GetInfo();
        info->sq_base_addr = sq_context->contextInfo.ubJfs.sqVa;
        info->sq_head_addr = sq_context->contextInfo.ubJfs.headAddr;
        info->sq_tail_addr = sq_context->contextInfo.ubJfs.tailAddr;
        info->sq_doorbell_addr = sq_context->contextInfo.ubJfs.dbVa;
        const auto cq_context = jetty_detail::GetCqContexts(channel) + HCOMM_URMA_DEFAULT_QP_IDX;
        info->cq_base_addr = cq_context->contextInfo.ubJfc.scqVa;
        info->cq_tail_addr = cq_context->contextInfo.ubJfc.tailAddr;
        info->cq_doorbell_addr = cq_context->contextInfo.ubJfc.dbVa;
        const auto head_addr = reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr);
        info->packed_head = static_cast<uint64_t>(asc_ldcg(head_addr));
        info->sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
        info->cq_depth = cq_context->contextInfo.ubJfc.cqDepth;
        info->cqe_bytes = cq_context->contextInfo.ubJfc.cqeSize;
        batch_begin_head = static_cast<uint32_t>(info->packed_head);
        last_write_wqebbs = 0;
        last_write_payload_wqebbs = 0;
    }

    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* GetInfo() const
    {
        return reinterpret_cast<__ubuf__ HcommJettyInfo*>(jetty_info_addr);
    }

    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* jetty_info, __ubuf__ uint8_t* ub_sqe, const uint64_t channel_addr)
    {
        channel = channel_addr;
        jetty_info_addr = reinterpret_cast<uint64_t>(jetty_info);
        ub_sqe_addr = reinterpret_cast<uint64_t>(ub_sqe);
        batch_begin_head = static_cast<uint32_t>(GetInfo()->packed_head);
        last_write_wqebbs = 0;
        last_write_payload_wqebbs = 0;
    }
public:

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteWarp(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");

        const int lane_idx = static_cast<int>(laneid());
        uint64_t packed_sge_addr = 0U;
        int packed_sge_len = 0;
        const int packed_sge_idx = lane_idx >= kNumWriteHeaderLanes
                                       ? (lane_idx - kNumWriteHeaderLanes) / 2
                                       : -1;
        const int num_valid_sges = GetWarpValidSge<kNumSGEs>(
            sge, packed_sge_idx, packed_sge_addr, packed_sge_len);
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);

        constexpr uint32_t kWriteWqebbs = NumWriteWqebbs<kNumSGEs>();
        uint32_t current_head = kInvalidHead;
        if (lane_idx == 0) {
            current_head = ReserveWrite(kWriteWqebbs);
        }
        current_head = asc_shfl(current_head, 0, kWarpSize);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);
        const uint32_t sq_depth = SqDepth();
        const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
        const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
        const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0U) << 31;
        const int num_write_words = kNumWriteHeaderLanes + num_valid_sges * 2;
        const int num_write_wqebbs = (num_write_words + kNumWQEBBLanes - 1) / kNumWQEBBLanes;
        if (lane_idx == 0) {
            last_write_wqebbs = kWriteWqebbs;
            last_write_payload_wqebbs = static_cast<uint32_t>(num_write_wqebbs);
        }

        const uint64_t sqe_word = BuildWriteSqeWord(
            peer, lane_idx, sq_wqebb_idx, owner_bit, dst, num_valid_sges, num_write_words,
            num_write_wqebbs, num_write_wqebbs, current_head, current_head, packed_sge_addr, packed_sge_len);
        const int total_words = static_cast<int>(kWriteWqebbs) * kNumWQEBBLanes;
        const int pair_lane = lane_idx & ~1;
        const int pair_source_lane = pair_lane + 1 < total_words ? pair_lane + 1 : total_words - 1;
        const uint64_t second_word = asc_shfl(sqe_word, pair_source_lane, kWarpSize);
        if (lane_idx < total_words && (lane_idx & 1) == 0) {
            auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(SqBaseAddr());
            StoreSqePair(sq_words, sq_depth, current_head, lane_idx, sqe_word, second_word);
        }
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalar(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs])
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");
        const int num_valid_sges = CountValidSges(sges);
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);
        constexpr uint32_t kWriteWqebbs = NumWriteWqebbs<kNumSGEs>();
        const uint32_t current_head = ReserveWrite(kWriteWqebbs);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);
        const uint32_t sq_depth = SqDepth();
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(SqBaseAddr());
        const int num_write_words = kNumWriteHeaderLanes + num_valid_sges * 2;
        const int num_write_wqebbs = (num_write_words + kNumWQEBBLanes - 1) / kNumWQEBBLanes;
        last_write_wqebbs = kWriteWqebbs;
        last_write_payload_wqebbs = static_cast<uint32_t>(num_write_wqebbs);
        for (int lane_idx = 0; lane_idx < static_cast<int>(kWriteWqebbs) * kNumWQEBBLanes; ++lane_idx) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0) << 31;
            uint64_t packed_sge_addr = 0U;
            int packed_sge_len = 0;
            if (lane_idx >= kNumWriteHeaderLanes && lane_idx < num_write_words) {
                GetValidSge(sges, (lane_idx - kNumWriteHeaderLanes) / 2, packed_sge_addr, packed_sge_len);
            }
            const uint64_t sqe_word = BuildWriteSqeWord(
                peer, lane_idx, sq_wqebb_idx, owner_bit, dst, num_valid_sges, num_write_words,
                num_write_wqebbs, num_write_wqebbs, current_head, current_head, packed_sge_addr, packed_sge_len);
            if ((lane_idx & 1) == 0) {
                uint64_t second_sge_addr = 0U;
                int second_sge_len = 0;
                const int second_word = lane_idx + 1;
                if (second_word >= kNumWriteHeaderLanes && second_word < num_write_words) {
                    GetValidSge(
                        sges, (second_word - kNumWriteHeaderLanes) / 2, second_sge_addr, second_sge_len);
                }
                const uint64_t second_value = BuildWriteSqeWord(
                    peer, second_word, sq_wqebb_idx, owner_bit, dst, num_valid_sges,
                    num_write_words, num_write_wqebbs, num_write_wqebbs, current_head, current_head,
                    second_sge_addr, second_sge_len);
                StoreSqePair(
                    sq_words, sq_depth, current_head, lane_idx, sqe_word, second_value);
            }
        }
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalarImmediate(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs])
    {
        PostWriteScalarImmediateImpl(peer, dst, sges, GetUbSqeWords());
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalarImmediate(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& first_sge)
    {
        PostWriteScalarImmediateSingle<kNumSGEs>(peer, dst, first_sge, GetUbSqeWords());
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteWarpImmediate(
        const HcommCoopWarp& warp, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge)
    {
        static_assert(NumWriteWqebbs<kNumSGEs>() == 2U, "Immediate DWQE path requires a fixed two-BB slot");
        const int lane_idx = static_cast<int>(laneid());
        uint64_t packed_sge_addr = 0;
        int packed_sge_len = 0;
        const int packed_sge_idx = lane_idx >= kNumWriteHeaderLanes
                                       ? (lane_idx - kNumWriteHeaderLanes) / 2
                                       : -1;
        const int num_valid_sges = GetWarpValidSge<kNumSGEs>(
            sge, packed_sge_idx, packed_sge_addr, packed_sge_len);
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);

        uint32_t current_head = kInvalidHead;
        if (lane_idx == 0) {
            current_head = ReserveWrite(2U, true);
        }
        current_head = asc_shfl(current_head, 0, kWarpSize);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);

        const uint32_t sq_depth = SqDepth();
        const uint32_t new_head = current_head + 2U;
        const uint32_t payload_wqebbs = num_valid_sges == 1 ? 1U : 2U;
        const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
        const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
        const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0U) << 31U;
        uint64_t sqe_word = 0;
        if (lane_idx == 0) {
            const uint32_t write_idx = payload_wqebbs == 2U ? new_head : current_head;
            sqe_word = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(write_idx & 0xFFFFU);
            if (payload_wqebbs == 2U) {
                sqe_word |= kWriteSQEFinalFlags;
            }
        } else if (lane_idx < kNumWriteHeaderLanes) {
            sqe_word = PeerHeaderWord(peer, lane_idx, dst, num_valid_sges);
        } else if (lane_idx < kNumWriteHeaderLanes + num_valid_sges * 2) {
            const bool is_len_word = ((lane_idx - kNumWriteHeaderLanes) % 2) == 0;
            sqe_word = is_len_word ? static_cast<uint64_t>(packed_sge_len) : packed_sge_addr;
        } else if (payload_wqebbs == 1U && lane_idx == kNumWQEBBLanes) {
            const uint64_t nop_owner_bit = static_cast<uint64_t>(((new_head - 1U) & sq_depth) == 0U) << 31U;
            sqe_word = kNopSQEHeaderTemplate | nop_owner_bit |
                       static_cast<uint64_t>(new_head & 0xFFFFU) | kNopSQEFinalFlags;
        }

        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(SqBaseAddr());
        auto* dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(DoorbellAddr() - kDwqeDbOffset);
        auto* dwqe_pairs = reinterpret_cast<__gm__ ulonglong2*>(dwqe_addr);
        // Every even lane owns one 128-bit pair. The DWQE is deliberately limited to its first
        // two WQEBBs; a larger WQE stays in SQ and is not copied past the DWQE window.
        const int pair_lane = lane_idx & ~1;
        const uint64_t second_word = asc_shfl(sqe_word, pair_lane + 1, kWarpSize);
        if (lane_idx < kNumDwqeWords && (lane_idx & 1) == 0) {
            const auto pair = make_ulonglong2(sqe_word, second_word);
            const uint32_t bb_offset = static_cast<uint32_t>(pair_lane / kNumWQEBBLanes);
            const uint32_t sq_idx = (current_head + bb_offset) & (sq_depth - 1U);
            auto* sq_pairs =
                reinterpret_cast<__gm__ ulonglong2*>(sq_words + static_cast<uint64_t>(sq_idx) * kNumWQEBBLanes);
            asc_stwt(sq_pairs + (pair_lane % kNumWQEBBLanes) / 2, pair);
        }

        // A single fence and warp barrier publish all SQ pairs before any lane exposes the
        // DWQE. Fencing inside the even-lane branch makes one post execute eight fences.
        asc_threadfence();
        warp.sync();

        if (lane_idx == 0) {
            PublishHead();
        }
        warp.sync();

        if (lane_idx < kNumDwqeWords && (lane_idx & 1) == 0) {
            const auto pair = make_ulonglong2(sqe_word, second_word);
            asc_stwt(&dwqe_pairs[pair_lane / 2], pair);
        }
        if (lane_idx == 0) {
            last_write_wqebbs = 2U;
            last_write_payload_wqebbs = payload_wqebbs;
            batch_begin_head = new_head;
        }
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalar(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& first_sge)
    {
        PostWriteScalarSingle<kNumSGEs>(peer, dst, first_sge);
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
        PostWriteCoop<kNumSGEs>(coop, peer, dst, sge);
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
        PutCoopImpl<kNumSGEs, kDoCommit>(coop, peer, dst, sge);
    }

    template <int kNumSGEs, bool kDoCommit = true, typename Coop>
    __simt_callee__ __forceinline__ void Put(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        PutCoopImpl<kNumSGEs, kDoCommit>(coop, peer, dst, sges);
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ __forceinline__ void Put(const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        PutScalarImpl<kNumSGEs, kDoCommit>(peer, dst, sge);
    }

    template <int kNumSGEs, bool kDoCommit = true>
    __simt_callee__ __forceinline__ void Put(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        PutScalarImpl<kNumSGEs, kDoCommit>(peer, dst, sges);
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
        const uint64_t packed_head = GetInfo()->packed_head;
        const uint32_t new_head = static_cast<uint32_t>(packed_head);
        const uint32_t batch_wqebbs = new_head - batch_begin_head;
        if (batch_wqebbs == 0U) {
            return;
        }
        const uint32_t sq_depth = SqDepth();
        HCOMM_JETTY_CHECK_OR_RETURN(
            batch_wqebbs <= sq_depth && last_write_wqebbs >= 2U && last_write_wqebbs <= kNumWQEBBsPerWrite &&
            last_write_wqebbs <= batch_wqebbs);

        const uint32_t last_write_head = new_head - last_write_wqebbs;
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(SqBaseAddr());
        const uint32_t last_write_idx = last_write_head & (sq_depth - 1U);
        auto* last_write_header = sq_words + static_cast<uint64_t>(last_write_idx) * kNumWQEBBLanes;
        asc_threadfence();
        const uint32_t payload_wqebbs = last_write_payload_wqebbs;
        HCOMM_JETTY_CHECK_OR_RETURN(payload_wqebbs > 0U && payload_wqebbs <= last_write_wqebbs);

        // Padding BBs are standalone NOP WQEs. If padding exists, the last NOP carries the
        // final PI/CQE/fence and is used as the doorbell; otherwise the WRITE itself does.
        const auto write_owner_bit = static_cast<uint64_t>((last_write_head & sq_depth) == 0U) << 31;
        uint64_t write_header = kWriteSQEHeaderTemplate | write_owner_bit;
        if (payload_wqebbs == last_write_wqebbs) {
            write_header |= static_cast<uint64_t>(new_head & 0xFFFFU) | kWriteSQEFinalFlags;
        } else {
            write_header |= static_cast<uint64_t>(last_write_idx);
            const uint32_t nop_head = new_head - 1U;
            const uint32_t nop_idx = nop_head & (sq_depth - 1U);
            const auto nop_owner_bit = static_cast<uint64_t>((nop_head & sq_depth) == 0U) << 31;
            auto* nop_header = sq_words + static_cast<uint64_t>(nop_idx) * kNumWQEBBLanes;
            asc_stwt(
                nop_header,
                kNopSQEHeaderTemplate | nop_owner_bit | static_cast<uint64_t>(new_head & 0xFFFFU) | kNopSQEFinalFlags);
        }
        asc_stwt(last_write_header, write_header);

        // The SQ is written with write-through stores. Keep the fence before the DWQE so the
        // device cannot observe the publish window before the complete SQ batch is visible.
        asc_threadfence();
        GetInfo()->packed_head += kCompletionDelta;
        PublishHead();
        auto* dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(DoorbellAddr() - kDwqeDbOffset);
        auto* dwqe_words = reinterpret_cast<__gm__ ulonglong2*>(dwqe_addr);
        // DWQE is always exactly two WQEBBs. A larger WQE remains complete in SQ and is fetched
        // from there after this 128-byte prefix publishes its header and PI.
        constexpr uint32_t kDwqeWqebbCount = 2U;
        constexpr uint32_t kDwqeWordCount = kDwqeWqebbCount * kNumWQEBBLanes;
        static_assert(sizeof(ulonglong2) == 2U * sizeof(uint64_t), "ulonglong2 must contain two 64-bit words");
        static_assert(kDwqeWordCount * sizeof(uint64_t) == 2U * kNumWQEBBBytes, "DWQE must be 128 bytes");
        constexpr uint32_t kDwqePairCount = kDwqeWordCount * sizeof(uint64_t) / sizeof(ulonglong2);
#pragma unroll
        for (uint32_t pair = 0; pair < kDwqePairCount; ++pair) {
            const uint32_t first_word = pair * 2U;
            const uint32_t first_bb = first_word / kNumWQEBBLanes;
            const uint32_t word_in_bb = first_word % kNumWQEBBLanes;
            const uint32_t source_idx = (last_write_head + first_bb) & (sq_depth - 1U);
            const auto source_words = sq_words + static_cast<uint64_t>(source_idx) * kNumWQEBBLanes + word_in_bb;
            // The SQ header was just changed to the final publication header. Read the
            // remaining words from SQ, but use the locally built header so a cached SQ load
            // cannot reintroduce the pre-commit header into the DWQE.
            const uint64_t first_value = first_word == 0U ? write_header : asc_ldcg(source_words);
            const uint64_t second_value = asc_ldcg(source_words + 1);
            asc_stwt(&dwqe_words[pair], make_ulonglong2(first_value, second_value));
        }
        batch_begin_head = new_head;
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ __forceinline__ void Drain() const
    {
        const uint64_t packed_head = GetInfo()->packed_head;
        PollCq<kNumTimeoutCycles>(static_cast<uint32_t>(packed_head >> 32U));
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
    static constexpr uint32_t kDwqeDbOffset = 0x80U;

    template <bool kImmediate = false>
    __simt_callee__ __forceinline__ static uint64_t BuildWriteSqeWord(
        const HcommPeer& peer, const int word_idx, const uint32_t sq_wqebb_idx,
        const uint64_t owner_bit, __gm__ void* dst, const int num_valid_sges, const int num_write_words,
        const int num_write_wqebbs, const int payload_wqebbs, const uint32_t current_head,
        const uint32_t new_head, const uint64_t packed_sge_addr, const int packed_sge_len)
    {
        if (word_idx == 0) {
            if constexpr (kImmediate) {
                const uint32_t write_idx = payload_wqebbs == 2 ? new_head : current_head;
                uint64_t header = kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(write_idx & 0xFFFFU);
                if (payload_wqebbs == 2) {
                    header |= kWriteSQEFinalFlags;
                }
                return header;
            }
            return kWriteSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        }
        if (word_idx < kNumWriteHeaderLanes) {
            return PeerHeaderWord(peer, word_idx, dst, num_valid_sges);
        }
        if (word_idx < num_write_words) {
            const bool is_len_word = ((word_idx - kNumWriteHeaderLanes) % 2) == 0;
            return is_len_word ? static_cast<uint64_t>(packed_sge_len) : packed_sge_addr;
        }
        if constexpr (kImmediate) {
            if (payload_wqebbs == 1 && word_idx == kNumWQEBBLanes) {
                return kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(new_head & 0xFFFFU) |
                       kNopSQEFinalFlags;
            }
        } else if (word_idx >= num_write_wqebbs * kNumWQEBBLanes && word_idx % kNumWQEBBLanes == 0) {
            return kNopSQEHeaderTemplate | owner_bit | static_cast<uint64_t>(sq_wqebb_idx);
        }
        return 0;
    }

    __simt_callee__ __forceinline__ static void StoreSqePair(
        __gm__ uint64_t* sq_words, const uint32_t sq_depth, const uint32_t current_head, const int first_word,
        const uint64_t first_value, const uint64_t second_value)
    {
        const uint32_t bb_offset = static_cast<uint32_t>(first_word / kNumWQEBBLanes);
        const uint32_t sq_idx = (current_head + bb_offset) & (sq_depth - 1U);
        auto* sq_pairs =
            reinterpret_cast<__gm__ ulonglong2*>(sq_words + static_cast<uint64_t>(sq_idx) * kNumWQEBBLanes);
        asc_stwt(
            sq_pairs + (first_word % kNumWQEBBLanes) / 2,
            make_ulonglong2(first_value, second_value));
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ static int CountValidSges(const HcommSge (&sges)[kNumSGEs])
    {
        int count = 0;
#pragma unroll
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            count += (sges[sge_idx].addr != 0U && sges[sge_idx].len > 0) ? 1 : 0;
        }
        return count;
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ static void GetValidSge(
        const HcommSge (&sges)[kNumSGEs], const int valid_sge_idx, uint64_t& addr, int& len)
    {
        if (valid_sge_idx < 0) {
            return;
        }
        int current_valid_sge_idx = 0;
#pragma unroll
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            if (sges[sge_idx].addr != 0U && sges[sge_idx].len > 0) {
                if (current_valid_sge_idx == valid_sge_idx) {
                    addr = sges[sge_idx].addr;
                    len = sges[sge_idx].len;
                    return;
                }
                ++current_valid_sge_idx;
            }
        }
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ static int GetWarpValidSge(
        const HcommSge& sge, const int valid_sge_idx, uint64_t& addr, int& len)
    {
        const uint32_t valid_mask = asc_ballot(
            static_cast<int32_t>(sge.addr != 0U && sge.len > 0)) & ((1U << kNumSGEs) - 1U);
        int num_valid_sges = 0;
        int source_lane = -1;
#pragma unroll
        for (int sge_idx = 0; sge_idx < kNumSGEs; ++sge_idx) {
            if (((valid_mask >> sge_idx) & 1U) != 0U) {
                if (num_valid_sges == valid_sge_idx) {
                    source_lane = sge_idx;
                }
                ++num_valid_sges;
            }
        }
        if (source_lane >= 0) {
            addr = asc_shfl(sge.addr, source_lane, kWarpSize);
            len = asc_shfl(sge.len, source_lane, kWarpSize);
        }
        return num_valid_sges;
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PostWriteScalarSingle(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge)
    {
        static_assert(kNumSGEs >= 2, "SIMT jetty requires at least two SGEs");
        static_assert(kNumSGEs <= kNumMaxSGEsPerWrite, "SIMT jetty SGE count exceeds the SQE layout");
        const int num_valid_sges = (sge.addr != 0U && sge.len > 0) ? 1 : 0;
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);

        constexpr uint32_t kWriteWqebbs = NumWriteWqebbs<kNumSGEs>();
        const uint32_t current_head = ReserveWrite(kWriteWqebbs);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);
        const uint32_t sq_depth = SqDepth();
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(SqBaseAddr());
        constexpr int kNumWriteWords = kNumWriteHeaderLanes + 2;
        constexpr int kNumPayloadWqebbs = (kNumWriteWords + kNumWQEBBLanes - 1) / kNumWQEBBLanes;
        last_write_wqebbs = kWriteWqebbs;
        last_write_payload_wqebbs = kNumPayloadWqebbs;

#pragma unroll
        for (int lane_idx = 0; lane_idx < static_cast<int>(kWriteWqebbs) * kNumWQEBBLanes; lane_idx += 2) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(lane_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0U) << 31;
            const uint64_t first_sge_addr = lane_idx == kNumWriteHeaderLanes ? sge.addr : 0U;
            const int first_sge_len = lane_idx == kNumWriteHeaderLanes ? sge.len : 0;
            const int second_word = lane_idx + 1;
            const uint64_t second_sge_addr = lane_idx == kNumWriteHeaderLanes ? sge.addr : 0U;
            const int second_sge_len = lane_idx == kNumWriteHeaderLanes ? sge.len : 0;
            const uint64_t first_value = BuildWriteSqeWord(
                peer, lane_idx, sq_wqebb_idx, owner_bit, dst, num_valid_sges, kNumWriteWords,
                kNumPayloadWqebbs, kNumPayloadWqebbs, current_head, current_head, first_sge_addr, first_sge_len);
            const uint64_t second_value = BuildWriteSqeWord(
                peer, second_word, sq_wqebb_idx, owner_bit, dst, num_valid_sges, kNumWriteWords,
                kNumPayloadWqebbs, kNumPayloadWqebbs, current_head, current_head, second_sge_addr, second_sge_len);
            StoreSqePair(sq_words, sq_depth, current_head, lane_idx, first_value, second_value);
        }
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ __forceinline__ void PostWriteCoop(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        coop.sync();
        auto* self = const_cast<HcommJetty*>(this);
        if constexpr (HcommCoopTraits<Coop>::kWarpCapable) {
            if constexpr (HcommCoopTraits<Coop>::kIsWarp) {
                self->template PostWriteWarp<kNumSGEs>(peer, dst, sge);
            } else if (coop.size() >= kWarpSize) {
                if (coop.thread_rank() < kWarpSize) {
                    self->template PostWriteWarp<kNumSGEs>(peer, dst, sge);
                }
            } else if (coop.thread_rank() == 0) {
                self->template PostWriteScalar<kNumSGEs>(peer, dst, sge);
            }
        } else if (coop.thread_rank() == 0) {
            self->template PostWriteScalar<kNumSGEs>(peer, dst, sge);
        }
        asc_threadfence();
        coop.sync();
    }

    template <int kNumSGEs, bool kDoCommit, typename Coop, typename SgeArg>
    __simt_callee__ __forceinline__ void PutCoopImpl(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const SgeArg& sges) const
    {
        if constexpr (kDoCommit && NumWriteWqebbs<kNumSGEs>() == 2U) {
            PutCoopImmediate<kNumSGEs>(coop, peer, dst, sges);
        } else {
            Write<kNumSGEs>(coop, peer, dst, sges);
            if constexpr (kDoCommit) {
                Commit(coop);
            }
        }
    }

    template <int kNumSGEs, bool kDoCommit, typename SgeArg>
    __simt_callee__ __forceinline__ void PutScalarImpl(
        const HcommPeer& peer, __gm__ void* dst, const SgeArg& sges) const
    {
        if constexpr (kDoCommit && NumWriteWqebbs<kNumSGEs>() == 2U) {
            PutScalarImmediate<kNumSGEs>(peer, dst, sges);
        } else {
            Write<kNumSGEs>(peer, dst, sges);
            if constexpr (kDoCommit) {
                const_cast<HcommJetty*>(this)->Commit();
            }
        }
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ __forceinline__ void PutCoopImmediate(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        coop.sync();
        auto* self = const_cast<HcommJetty*>(this);
        if constexpr (HcommCoopTraits<Coop>::kWarpCapable) {
            if constexpr (HcommCoopTraits<Coop>::kIsWarp) {
                self->template PostWriteWarpImmediate<kNumSGEs>(coop, peer, dst, sge);
            } else if (coop.size() >= kWarpSize) {
                HcommCoopWarp warp;
                if (coop.thread_rank() < kWarpSize) {
                    self->template PostWriteWarpImmediate<kNumSGEs>(warp, peer, dst, sge);
                }
            } else if (coop.thread_rank() == 0) {
                self->template PostWriteScalarImmediate<kNumSGEs>(peer, dst, sge);
            }
        } else if (coop.thread_rank() == 0) {
            self->template PostWriteScalarImmediate<kNumSGEs>(peer, dst, sge);
        }
        asc_threadfence();
        coop.sync();
    }

    template <int kNumSGEs, typename Coop>
    __simt_callee__ __forceinline__ void PutCoopImmediate(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        coop.sync();
        if (coop.thread_rank() == 0) {
            const_cast<HcommJetty*>(this)->template PostWriteScalarImmediate<kNumSGEs>(peer, dst, sges);
        }
        asc_threadfence();
        coop.sync();
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PutScalarImmediate(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalarImmediate<kNumSGEs>(peer, dst, sge);
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ void PutScalarImmediate(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs]) const
    {
        const_cast<HcommJetty*>(this)->template PostWriteScalarImmediate<kNumSGEs>(peer, dst, sges);
    }

    __simt_callee__ __forceinline__ __ubuf__ uint64_t* GetUbSqeWords() const
    {
        return reinterpret_cast<__ubuf__ uint64_t*>(ub_sqe_addr);
    }

    template <int kNumSGEs, typename StagePtr>
    __simt_callee__ __forceinline__ void PostWriteScalarImmediateImpl(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge (&sges)[kNumSGEs], StagePtr stage)
    {
        static_assert(NumWriteWqebbs<kNumSGEs>() == 2U, "Immediate DWQE path requires a fixed two-BB slot");
        const int num_valid_sges = CountValidSges(sges);
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);
        const uint32_t current_head = ReserveWrite(2U, true);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);

        const uint32_t sq_depth = SqDepth();
        const uint32_t new_head = current_head + 2U;
        const uint32_t payload_wqebbs = num_valid_sges == 1 ? 1U : 2U;
#pragma unroll
        for (int word_idx = 0; word_idx < kNumDwqeWords; ++word_idx) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(word_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0U) << 31U;
            uint64_t packed_sge_addr = 0U;
            int packed_sge_len = 0;
            if (word_idx >= kNumWriteHeaderLanes && word_idx < kNumWriteHeaderLanes + num_valid_sges * 2) {
                GetValidSge(sges, (word_idx - kNumWriteHeaderLanes) / 2, packed_sge_addr, packed_sge_len);
            }
            stage[word_idx] = BuildWriteSqeWord<true>(
                peer, word_idx, sq_wqebb_idx, owner_bit, dst, num_valid_sges,
                kNumWriteHeaderLanes + num_valid_sges * 2, 2U, payload_wqebbs,
                current_head, new_head, packed_sge_addr, packed_sge_len);
        }

        CopyTwoBbStageToSq(SqBaseAddr(), sq_depth, current_head, stage);
        PublishHead();
        RingDwqe(DoorbellAddr(), stage);
        last_write_wqebbs = 2U;
        last_write_payload_wqebbs = payload_wqebbs;
        batch_begin_head = new_head;
    }

    template <int kNumSGEs, typename StagePtr>
    __simt_callee__ __forceinline__ void PostWriteScalarImmediateSingle(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge, StagePtr stage)
    {
        static_assert(NumWriteWqebbs<kNumSGEs>() == 2U, "Immediate DWQE path requires a fixed two-BB slot");
        const int num_valid_sges = (sge.addr != 0U && sge.len > 0) ? 1 : 0;
        HCOMM_JETTY_CHECK_OR_RETURN(num_valid_sges > 0);
        const uint32_t current_head = ReserveWrite(2U, true);
        HCOMM_JETTY_CHECK_OR_RETURN(current_head != kInvalidHead);

        const uint32_t sq_depth = SqDepth();
        const uint32_t new_head = current_head + 2U;
        constexpr int kNumPayloadWqebbs = (kNumWriteHeaderLanes + 2 + kNumWQEBBLanes - 1) / kNumWQEBBLanes;
#pragma unroll
        for (int word_idx = 0; word_idx < kNumDwqeWords; ++word_idx) {
            const uint32_t wqebb_head = current_head + static_cast<uint32_t>(word_idx / kNumWQEBBLanes);
            const uint32_t sq_wqebb_idx = wqebb_head & (sq_depth - 1U);
            const uint64_t owner_bit = static_cast<uint64_t>((wqebb_head & sq_depth) == 0U) << 31U;
            const bool is_sge_word = word_idx >= kNumWriteHeaderLanes && word_idx < kNumWriteHeaderLanes + 2;
            const uint64_t packed_sge_addr = is_sge_word ? sge.addr : 0U;
            const int packed_sge_len = is_sge_word ? sge.len : 0;
            stage[word_idx] = BuildWriteSqeWord<true>(
                peer, word_idx, sq_wqebb_idx, owner_bit, dst, num_valid_sges,
                kNumWriteHeaderLanes + 2, 2U, kNumPayloadWqebbs,
                current_head, new_head, packed_sge_addr, packed_sge_len);
        }

        CopyTwoBbStageToSq(SqBaseAddr(), sq_depth, current_head, stage);
        PublishHead();
        RingDwqe(DoorbellAddr(), stage);
        last_write_wqebbs = 2U;
        last_write_payload_wqebbs = kNumPayloadWqebbs;
        batch_begin_head = new_head;
    }

    __simt_callee__ __forceinline__ uint64_t SqBaseAddr() const
    {
        return GetInfo()->sq_base_addr;
    }

    __simt_callee__ __forceinline__ uint64_t SqHeadAddr() const
    {
        return GetInfo()->sq_head_addr;
    }

    __simt_callee__ __forceinline__ uint64_t SqTailAddr() const
    {
        return GetInfo()->sq_tail_addr;
    }

    __simt_callee__ __forceinline__ uint64_t DoorbellAddr() const
    {
        return GetInfo()->sq_doorbell_addr;
    }

    __simt_callee__ __forceinline__ uint64_t CqBaseAddr() const
    {
        return GetInfo()->cq_base_addr;
    }

    __simt_callee__ __forceinline__ uint64_t CqTailAddr() const
    {
        return GetInfo()->cq_tail_addr;
    }

    __simt_callee__ __forceinline__ uint64_t CqDoorbellAddr() const
    {
        return GetInfo()->cq_doorbell_addr;
    }

    __simt_callee__ __forceinline__ uint32_t SqDepth() const
    {
        return GetInfo()->sq_depth;
    }

    __simt_callee__ __forceinline__ uint32_t CqDepth() const
    {
        return GetInfo()->cq_depth;
    }

    __simt_callee__ __forceinline__ uint32_t CqeBytes() const
    {
        return GetInfo()->cqe_bytes;
    }

    template <typename StagePtr>
    __simt_callee__ __forceinline__ static void CopyTwoBbStageToSq(
        const uint64_t sq_base_addr, const uint32_t sq_depth, const uint32_t current_head, StagePtr stage)
    {
        auto* sq_words = reinterpret_cast<__gm__ uint64_t*>(sq_base_addr);
#pragma unroll
        for (int pair_idx = 0; pair_idx < kNumDwqeWords / 2; ++pair_idx) {
            const int first_word = pair_idx * 2;
            const uint32_t bb_offset = static_cast<uint32_t>(first_word / kNumWQEBBLanes);
            const uint32_t sq_idx = (current_head + bb_offset) & (sq_depth - 1U);
            auto* sq_pairs =
                reinterpret_cast<__gm__ ulonglong2*>(sq_words + static_cast<uint64_t>(sq_idx) * kNumWQEBBLanes);
            asc_stwt(
                sq_pairs + (first_word % kNumWQEBBLanes) / 2,
                make_ulonglong2(stage[first_word], stage[first_word + 1]));
        }
    }

    template <typename StagePtr>
    __simt_callee__ __forceinline__ static void RingDwqe(const uint64_t doorbell_addr, StagePtr stage)
    {
        auto* dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(doorbell_addr - kDwqeDbOffset);
        auto* dwqe_pairs = reinterpret_cast<__gm__ ulonglong2*>(dwqe_addr);
        asc_threadfence();
#pragma unroll
        for (int pair_idx = 0; pair_idx < kNumDwqeWords / 2; ++pair_idx) {
            const int first_word = pair_idx * 2;
            asc_stwt(&dwqe_pairs[pair_idx], make_ulonglong2(stage[first_word], stage[first_word + 1]));
        }
    }

    __simt_callee__ __forceinline__ static uint64_t PeerHeaderWord(
        const HcommPeer& peer, const int lane_idx, __gm__ void* dst, const int num_sges)
    {
        if (lane_idx == 1) {
            const auto* info = peer.GetInfo();
            return (info->tp_id_num_sges_rmt_token_id & 0x0000000000FFFFFFULL) |
                   (static_cast<uint64_t>(num_sges) << 24U) |
                   (info->tp_id_num_sges_rmt_token_id & 0x000FFFFF00000000ULL);
        }
        if (lane_idx == 2 || lane_idx == 3) {
            return peer.GetInfo()->rmt_eid[lane_idx - 2];
        }
        if (lane_idx == 4) {
            return peer.GetInfo()->rmt_token_value_udf;
        }
        return reinterpret_cast<uint64_t>(dst);
    }

    template <int kNumSGEs>
    __simt_callee__ __forceinline__ static constexpr uint32_t NumWriteWqebbs()
    {
        constexpr uint32_t kNumWords = kNumWriteHeaderLanes + static_cast<uint32_t>(kNumSGEs) * 2U;
        constexpr uint32_t kNumWqebbs = (kNumWords + kNumWQEBBLanes - 1U) / kNumWQEBBLanes;
        return kNumWqebbs < 2U ? 2U : kNumWqebbs;
    }

    __simt_callee__ __forceinline__ uint32_t
    ReserveWrite(const uint32_t num_wqebbs, const bool count_completion = false) const
    {
        static constexpr uint32_t kMaxReservationRetries = 1024U;
        const auto tail_addr = reinterpret_cast<__gm__ uint32_t*>(SqTailAddr());
        const uint32_t sq_depth = SqDepth();
        auto* ub_head_addr = reinterpret_cast<__ubuf__ uint32_t*>(&GetInfo()->packed_head);
        for (uint32_t retry = 0; retry < kMaxReservationRetries; ++retry) {
            const uint32_t current_head = *ub_head_addr;
            const uint32_t used_wqebbs = current_head - asc_ldcg(tail_addr);
            if (used_wqebbs > sq_depth || num_wqebbs > sq_depth - used_wqebbs) {
                return kInvalidHead;
            }
            if (asc_atomic_cas(ub_head_addr, current_head, current_head + num_wqebbs) == current_head) {
                if (count_completion) {
                    GetInfo()->packed_head += 1ULL << 32U;
                }
                return current_head;
            }
        }
        return kInvalidHead;
    }

    __simt_callee__ __forceinline__ void PublishHead() const
    {
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(SqHeadAddr()), GetInfo()->packed_head);
    }

    template <int64_t kNumTimeoutCycles>
    __simt_callee__ __forceinline__ void PollCq(const uint32_t expected_tail) const
    {
        static_assert(kNumTimeoutCycles > 0, "SIMT jetty CQ timeout must be positive");
        static constexpr uint32_t kNumCQDoorbellBits = 24;
        static constexpr uint32_t kCQEOwnerBit = 2;
        auto* cq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(CqTailAddr());
        auto* sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(SqTailAddr());
        uint32_t current_tail = *cq_tail_addr;
        if (current_tail == expected_tail) {
            return;
        }
        uint32_t sq_tail = *sq_tail_addr;
        const uint32_t cq_depth = CqDepth();
        const uint32_t sq_depth = SqDepth();
        while (current_tail != expected_tail) {
            const auto cqe = reinterpret_cast<__gm__ uint32_t*>(
                CqBaseAddr() + static_cast<uint64_t>(CqeBytes()) * (current_tail & (cq_depth - 1U)));
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
        *reinterpret_cast<__gm__ uint32_t*>(CqDoorbellAddr()) =
            current_tail & ((1U << kNumCQDoorbellBits) - 1U);
        *sq_tail_addr = sq_tail;
    }
};
static_assert(sizeof(HcommJetty) == 40, "SIMT jetty handle layout mismatch");

} // namespace jetty
} // namespace simt

#endif

#undef HCOMM_JETTY_CHECK_OR_RETURN

} // namespace AscendC

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
