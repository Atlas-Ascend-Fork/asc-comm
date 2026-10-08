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

/*!
 * @class Hcomm
 * @brief The SIMT counterpart of AscendC::Hcomm. It provides the same point-to-point
 *        communication primitives (WriteNbi, ReadNbi and so on) for code running in the SIMT
 *        execution space, where LocalTensor and the pipe synchronization primitives are not
 *        available.
 *        The typical usage of this class is as follows:
 *          1) Create Hcomm object.
 *          2) Launch comm tasks asynchronously through the corresponding interface.
 *          3) Call the Drain interface (blocking) to wait for completion of the comm tasks.
 * @tparam commProtocol: The communication protocol to use, UB_CTP supported as default.
 * @note Unlike the SIMD path, the doorbell is rung by copying a 128-byte DWQE rather than writing
 *       a scalar register. A deferred Write/Read occupies one SQ basic block, while an immediate
 *       Write/Read and the two-BB Notify/atomic WQEs occupy two.
 * @note There is no Commit interface on SIMT. A task launched with commit set to false stays in the
 *       send queue until a later task launched with commit set to true carries it out, because that
 *       task's doorbell publishes a producer index covering the whole batch.
 * @note An Hcomm object is lane-private. Non-batch interfaces are called by one lane. On a
 *       batch overload with a Group argument, one lane plans the SQ range, every
 *       lane writes its final slot, and BatchCommit publishes after those writes are ready.
 * @note When the send queue has insufficient free basic blocks, a posting interface polls completed
 *       CQEs to release SQ space and retries the reservation. It returns -1 without changing the SQ
 *       head if no completion arrives before the retry limit. Deferred posts reserve two basic blocks
 *       for the later immediate DWQE that publishes the batch.
 * @note A channel must not be driven by both this class and the SIMD AscendC::Hcomm. The two paths
 *       keep their queue state in different places: SIMD uses the counters in ChannelEntity, SIMT
 *       packs curHead and wqeCnt into the u64 at SqContext::ubJfs::headAddr.
 */
template <CommProtocol commProtocol = COMM_PROTOCOL_UB_CTP>
class Hcomm {
public:
    __simt_callee__ inline Hcomm();

    __simt_callee__ inline ~Hcomm();

    /*!
     * @brief Initialize Hcomm.
     * @param [in] buff: Not used. Pass nullptr.
     * @param [in] len: Not used. Pass 0.
     * @return 0 indicates success and -1 indicates failure.
     * @note Every posting lane calls Init on its own Hcomm object. The shared UB workspace for
     *       a group batch is passed to MakeBatchHandle, not Init.
     */
    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

    /*!
     * @brief Create an explicit batch bound to one remote registration.
     * @param [in] channel: Channel exclusively used by this batch handle.
     * @param [in] buff: Caller-owned UB storage for the shared context and publisher image.
     * @param [in] buffLen: Size of buff in bytes; reserve at least 256 bytes.
     * @param [in] remoteBase: An address in the remote registration used by the batch.
     * @param [in] itemBb: Fixed WQEBB count of every request submitted through this handle (1 or 2).
     * @note The 256-byte workspace holds a 128-byte shared context and a 128-byte publisher
     *       DWQE image. Each lane writes its request directly to the SQ, so the workspace
     *       size does not grow with the group size.
     * @note On failure this function returns a zero-valued handle (context == nullptr). The
     *       caller must check it before any batch posting, BatchCommit or Drain operation.
     * @note Every remote data and notify range appended through the returned handle must stay in
     *       the registration selected by remoteBase. The caller owns this contract.
     * @note The handle keeps itemBb and the group size fixed for its lifetime. Every batch contains
     *       exactly one request per lane. A channel is exclusively owned by one live batch handle.
     *       itemBb must match the actual WQE width of each request; mixing widths is unsupported.
     */
    __simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb = 1U);

    /*!
     * @brief Create a batch for the explicitly supplied cooperative group.
     * @param [in] group: Cooperative group whose lanes own this batch.
     * @note Pass the same group to every posting call and BatchCommit for this handle.
     *       The no-group overloads are for single-lane handles. A supplied Group uses the
     *       collective path, including when its current size is 1.
     *       Use fixed group membership, lane ranks and size for the handle's lifetime.
     *       Every lane must pass the same valid buff, buffLen and itemBb arguments.
     */
    template <typename Group>
    __simt_callee__ inline BatchHandle<ChannelHandle> MakeBatchHandle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb,
        const Group& group);

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

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    __simt_callee__ inline int32_t WriteNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group);

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

    template <typename T, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value);

    template <typename T, auto const& config = URMA_INLINE_CFG, typename Group>
    __simt_callee__ inline int32_t WriteValueNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value, const Group& group);

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

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    __simt_callee__ inline int32_t ReadNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group);

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

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal);

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal, const Group& group);

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

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal);

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
    __simt_callee__ inline int32_t AtomicFAA(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal, const Group& group);

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

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
    __simt_callee__ inline int32_t AtomicCAS(
        UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
        const Group& group);

    /*!
     * @brief Publish the SQ range prepared by the preceding batch posting call.
     * @note The overload with a Group argument is collective: every group lane must call it and
     *       prepare exactly one WQE.
     * @note The final item must request a CQE. If the preceding posting call fails to reserve SQ
     *       space, the caller must not call BatchCommit for that batch.
     */
    __simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle);

    template <typename Group>
    __simt_callee__ inline int32_t BatchCommit(UbcCtpBatchHandle& batchHandle, const Group& group);

    /*!
     * @brief Block and drain comm tasks submitted on channel until finish processing.
     * @tparam pipe: Unused on SIMT.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called by a single lane, after every posting lane has returned.
     */
    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(ChannelHandle channel);

    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(UbcCtpBatchHandle& batchHandle);

    /*!
     * @brief Completion ownership for channels sharing one or more CQs (version 1).
     * @note Initialize once on fresh, unused channels with 128-byte-aligned, zeroed GM storage
     *       sized by HcommSimtCompletionBufferBytes(count). Keep storage until all work finishes.
     *       Register every user of each CQ. Exactly one lane owns ALL set operations. Drain,
     *       automatic ReservePost polling, SIMD and other consumers must not access these CQs.
     * @note PrepareCompletion admits one window per channel BEFORE publishing, using its WQE
     *       count and total SQ BB count. Only one window per channel may be active; other channels
     *       need not finish. Insufficient capacity returns -1 without mutation.
     *       Publish exactly this window using default CQE-enabled Write/Read/WriteWithNotify batches. Sparse CQEs,
     *       deferred unpublished tails and unsignalled NOP tails are unsupported. Cumulative
     *       WQE/BB counters must not wrap. The caller is responsible for publication ordering.
     *       If an existing batch's next reservation failed before reclamation, recreate its handle
     *       with MakeBatchHandle after this window completes, following that group's contract.
     *       CompletionSet does not mutate the publishing group's UB reservation context.
     * @note Progress consumes at most budget ready CQEs without waiting for an unready CQE.
     *       Completion/SQ space are attributed to their JFS. WaitCompletion waits only for the
     *       selected channel's admitted target, while progressing all CQs. Drain waits for all
     *       admitted targets and is optional. None of these functions is a collective barrier.
     *       Other Jettys can publish their admitted windows concurrently; ownership of the set
     *       itself must not be concurrent. There is no background progress or posting-path lock.
     * @note A completed window releases its SQ up to its recorded BB endpoint. Known-safe CQ
     *       prefixes are acknowledged even on failure; a fault poisons the set, requiring teardown.
     *       The workspace records resource, identity, CQE, count and timeout evidence for diagnosis.
     */
    __simt_callee__ inline int32_t InitCompletionSet(
        UbcCtpCompletionSet& set, __gm__ ChannelHandle* channels, uint32_t count, __gm__ uint8_t* workspace,
        uint64_t bytes);
    __simt_callee__ inline int32_t PrepareCompletion(
        UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount);
    __simt_callee__ inline int32_t Progress(UbcCtpCompletionSet& set, uint32_t budget, uint32_t& processed);
    __simt_callee__ inline int32_t WaitCompletion(
        UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t maxIdlePolls = 1000000U);
    __simt_callee__ inline int32_t Drain(UbcCtpCompletionSet& set);

private:
    HcommImpl<commProtocol> impl_;
};

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
