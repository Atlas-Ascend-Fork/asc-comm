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
 * \file hcomm_simt.h
 * \brief Hcomm SIMT interface
 */

#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_H

#include <cstdint>

#include "hcomm_common.h"

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_H
#endif
#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_simt_impl_def.h"

namespace AscendC::simt {

namespace coop_detail {
/*!
 * @brief Thread-level coop (single lane). Equivalent to NCCL ncclCoopThread.
 * @note  thread_rank()=0, size()=1, sync()=no-op.
 */
struct Thread {
    int thread_rank() const { return 0; }
    int size() const { return 1; }
    int num_threads() const { return 1; }
    void sync() {}
};

/*!
 * @brief Warp-level coop (32 lanes). Equivalent to NCCL ncclCoopWarp.
 * @note  Wraps cooperative_groups::thread_block_tile<32>.
 */
struct Warp {
    int thread_rank() const { return static_cast<int>(laneid()); }
    int size() const { return 32; }
    int num_threads() const { return 32; }
    void sync() { asc_threadfence_block(); }
};

/*!
 * @brief Block-level coop (entire CTU/block). Equivalent to NCCL ncclCoopCta.
 * @note  Wraps cooperative_groups::thread_block.
 */
struct Block {
    int thread_rank() const { return static_cast<int>(blockIdx_local()); }
    int size() const { return static_cast<int>(blockDim_global()); }
    int num_threads() const { return static_cast<int>(blockDim_global()); }
    void sync() { asc_syncthreads(); }
};

/*!
 * @brief Tile of N lanes within a warp. Equivalent to NCCL ncclCoopTile<N>.
 * @tparam N: Power-of-2 tile size (1, 2, 4, 8, 16, 32).
 */
template <int N>
struct Tile {
    static_assert(N > 0 && N <= 32 && (N & (N - 1)) == 0, "N must be power of 2 in [1,32]");
    int thread_rank() const { return static_cast<int>(laneid()) & (N - 1); }
    int size() const { return N; }
    int num_threads() const { return N; }
    void sync() { asc_threadfence_block(); }
};

/*!
 * @brief Coalesced lanes (active mask). Equivalent to NCCL ncclCoopLanes.
 */
struct Lanes {
    unsigned int lmask;
    Lanes(unsigned int mask = 0xFFFFFFFF) : lmask(mask) {}
    int thread_rank() const { return __popc(lmask & (1u << laneid()) - 1); }
    int size() const { return __popc(lmask); }
    int num_threads() const { return __popc(lmask); }
    void sync() { asc_threadfence_block(); }
};
} // namespace coop_detail

/*!
 * @brief Cooperative group type for single-lane operation (default).
 *        Matches NCCL ncclCoopThread semantics: thread_rank()=0, size()=1, sync()=no-op.
 */
using HcommCoopThread = coop_detail::Thread;

/*!
 * @brief Cooperative group type for single warp (32 lanes).
 *        Matches NCCL ncclCoopWarp semantics.
 */
using HcommCoopWarp = coop_detail::Warp;

/*!
 * @brief Cooperative group type for entire block (all threads).
 *        Matches NCCL ncclCoopCta semantics.
 */
using HcommCoopBlock = coop_detail::Block;

/*!
 * @brief Cooperative group template for a tile of N lanes within a warp.
 *        N must be power of 2: 1, 2, 4, 8, 16, or 32.
 */
template <int N>
using HcommCoopTile = coop_detail::Tile<N>;

/*!
 * @brief Cooperative group type for active lane mask.
 *        Matches NCCL ncclCoopLanes semantics.
 */
using HcommCoopLanes = coop_detail::Lanes;

/*!
 * @brief Default unbound group for backward compatibility with per-lane API.
 */
using HcommUnboundGroup = coop_detail::Thread;

/*!
 * @class Hcomm
 * @brief The SIMT counterpart of AscendC::Hcomm. It provides the same point-to-point
 *        communication primitives (WriteNbi, ReadNbi and so on) for code running in the SIMT
 *        execution space, where LocalTensor and the pipe synchronization primitives are not
 *        available.
 *        The typical usage of this class is as follows:
 *          1) Create Hcomm object, optionally binding it to a cooperative group.
 *          2) Launch comm tasks asynchronously through the corresponding interface.
 *          3) Call the Drain interface (blocking) to wait for completion of the comm tasks.
 * @tparam commProtocol: The communication protocol to use, UBC_CTP supported as default.
 * @tparam Group: The cooperative group type. All communication APIs follow the
 *         sync-act-sync pattern using the group: only the lane with thread_rank()==0
 *         issues the operation, bracketed by group.sync() calls.
 *         Default (HcommUnboundGroup) is equivalent to single-lane operation.
 *         Users may pass cooperative_groups::thread_block, thread_block_tile<N>, or
 *         coalesced_group for group-level communication.
 * @note Unlike the SIMD path, the doorbell is rung by copying a 128-byte DWQE rather than writing
 *       a scalar register. A deferred Write/Read occupies one SQ basic block, while an immediate
 *       Write/Read and the two-BB Notify/atomic WQEs occupy two.
 * @note There is no Commit interface on SIMT. A task launched with commit set to false stays in the
 *       send queue until a later task launched with commit set to true carries it out, because that
 *       task's doorbell publishes a producer index covering the whole batch.
 * @note When a cooperative group of size > 1 is bound, only the lane with thread_rank()==0 actually
 *       issues the communication operation; all other lanes in the group participate in sync() but
 *       do not post to the send queue. A channel must not carry deferred tasks from more than one
 *       lane that shares the same group and posts through this object.
 * @note When the send queue has insufficient free basic blocks, a posting interface polls completed
 *       CQEs to release SQ space and retries the reservation. It returns -1 without changing the SQ
 *       head if no completion arrives before the retry limit. Deferred posts reserve two basic blocks
 *       for the later immediate DWQE that publishes the batch.
 * @note A channel must not be driven by both this class and the SIMD AscendC::Hcomm. The two paths
 *       keep their queue state in different places: SIMD uses the counters in ChannelEntity, SIMT
 *       packs curHead and wqeCnt into the u64 at SqContext::ubJfs::headAddr.
 */
template <CommProtocol commProtocol = COMM_PROTOCOL_UBC_CTP, typename Group = HcommUnboundGroup>
class Hcomm {
public:
    __simt_callee__ inline Hcomm();

    /*!
     * @brief Construct Hcomm and bind it to a cooperative group.
     * @param [in] group: The cooperative group that controls group-level communication.
     *        All communication APIs use the sync-act-sync pattern:
     *        group.sync() -> if thread_rank()==0: issue op -> group.sync()
     */
    __simt_callee__ inline explicit Hcomm(const Group& group);
    __simt_callee__ inline ~Hcomm();

    /*!
     * @brief Initialize Hcomm.
     * @param [in] buff: Workspace buffer (unused in current implementation).
     * @param [in] len: Workspace buffer length (unused in current implementation).
     * @return 0 indicates success and -1 indicates failure.
     * @note No workspace is required: a WQE is staged in lane-private storage for the duration of a
     *       post, and every post resolves its channel from global memory. Every lane that posts must
     *       call Init on its own object. The buff and len parameters are kept for API compatibility
     *       but are ignored.
     */
    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

    /*!
     * @brief The task launching interface of the Write point-to-point communication operator.
     *        (task content: Write data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. len must fit in 32 bits.
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);

    /*!
     * @brief The task launching interface of the inline Write point-to-point communication operator.
     *        The source data is provided by value and carried inline in the WQE.
     * @tparam T: The value type to write.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config.
     *         Default: strongly ordered + fence + CQE + inline enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] value: The inline value to write.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. config must have inline enabled, and
     *       sizeof(T) must fit in the WQE inline payload area.
     */
    template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value);

    /*!
     * @brief The task launching interface of the Read point-to-point communication operator.
     *        (task content: Read data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to read, using byte as the basic unit.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. len must fit in 32 bits.
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);

    /*!
     * @brief The task launching interface of the Write-with-notify point-to-point communication operator.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit.
     * @param [in] notifyAddr: The remote notify address.
     * @param [in] notifyVal: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal);

    /*!
     * @brief The task launching interface of the Fetch-and-add point-to-point communication operator.
     * @tparam T: The data type of the atomic operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetchAddr: The address to store the old value before atomic add.
     * @param [in] addVal: The remote add value.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal);

    /*!
     * @brief The task launching interface of the Compare-and-swap point-to-point communication operator.
     * @tparam T: The data type of the atomic operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: URMA WQE control config. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetchAddr: The address to store the old value before atomic compare-and-swap.
     * @param [in] compareVal: The value to compare against the value at the destination address.
     * @param [in] swapVal: The value to swap into the destination address if comparison succeeds.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);

    /*!
     * @brief Block and drain comm tasks submitted on channel until finish processing.
     * @tparam pipe: Unused on SIMT, kept for signature compatibility with the SIMD interface.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called by a single lane, after every posting lane has returned.
     */
    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(ChannelHandle channel);

private:
    Group group_;
    HcommImpl<commProtocol> impl_;
};

template <typename Group>
Hcomm(const Group&) -> Hcomm<COMM_PROTOCOL_UBC_CTP, Group>;

/*!
 * @brief Resolve the base address of a locally registered buffer on the channel.
 * @param [in] channel: The handle of the communication channel.
 * @param [in] bufferIdx: Index into the channel's local registered buffer table.
 * @return Base address of the requested local buffer.
 */
__simt_callee__ inline __gm__ uint8_t* LocalBufferAddr(ChannelHandle channel, uint32_t bufferIdx);

/*!
 * @brief Resolve the base address of a remotely registered buffer on the channel.
 * @param [in] channel: The handle of the communication channel.
 * @param [in] bufferIdx: Index into the channel's remote registered buffer table.
 * @return Base address of the requested remote buffer.
 */
__simt_callee__ inline __gm__ uint8_t* RemoteBufferAddr(ChannelHandle channel, uint32_t bufferIdx);

} // namespace AscendC::simt

#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_simt_impl.h"

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_H
#endif

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_H
