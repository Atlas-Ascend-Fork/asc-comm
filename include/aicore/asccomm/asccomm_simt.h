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
 * \file asccomm_simt.h
 * \brief asccomm SIMT interface
 *
 * This header provides class asc::simt::asccomm with the new asccomm* point-to-point
 * communication interfaces for the SIMT
 * execution space. The legacy Hcomm SIMT interface names (Init, WriteNbi, ReadNbi and so on)
 * remain available through hcomm/hcomm_simt.h. Both sets share the same underlying
 * implementation; legacy interfaces are marked as deprecated and forward
 * to the corresponding asccomm* interface.
 */

#ifndef INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_SIMT_H
#define INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_SIMT_H

#include <cstdint>

#include "asccomm_common.h"

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_H
#endif
#include "../../../../impl/comm_api/aicore/asccomm/impl/asccomm_simt_impl_def.h"

namespace asc::simt {

/*!
 * @class asccomm
 * @brief The SIMT counterpart of AscendC::Hcomm. It provides the same point-to-point
 *        communication primitives (write_nbi, read_nbi and so on) for code running in the SIMT
 *        execution space, where LocalTensor and the pipe synchronization primitives are not
 *        available.
 *        The typical usage of this class is as follows:
 *          1) Create asccomm object.
 *          2) Launch comm tasks asynchronously through the corresponding interface.
 *          3) Call the drain interface (blocking) to wait for completion of the comm tasks.
 * @tparam comm_protocol: The communication protocol to use, UB_CTP supported as default.
 * @note Unlike the SIMD path, the doorbell is rung by copying a 128-byte DWQE rather than writing
 *       a scalar register. A deferred Write/Read occupies one SQ basic block, while an immediate
 *       Write/Read and the two-BB Notify/atomic WQEs occupy two.
 * @note There is no commit interface on SIMT. A task launched with commit set to false stays in the
 *       send queue until a later task launched with commit set to true carries it out, because that
 *       task's doorbell publishes a producer index covering the whole batch.
 * @note An asccomm object is lane-private. Non-batch interfaces are called by one lane. On a
 *       batch overload with a coop_group argument, one lane plans the SQ range, every
 *       lane writes its final slot, and batch_commit publishes after those writes are ready.
 * @note When the send queue has insufficient free basic blocks, a posting interface polls completed
 *       CQEs to release SQ space and retries the reservation. It returns -1 without changing the SQ
 *       head if no completion arrives before the retry limit. Deferred posts reserve two basic blocks
 *       for the later immediate DWQE that publishes the batch.
 * @note A channel must not be driven by both this class and the SIMD AscendC::Hcomm. The two paths
 *       keep their queue state in different places: SIMD uses the counters in ChannelEntity, SIMT
 *       packs cur_head and wqe_cnt into the u64 at SqContext::ubJfs::headAddr.
 */
template <CommProtocol comm_protocol = COMM_PROTOCOL_UB_CTP>
class asccomm {
public:
    __simt_callee__ inline asccomm();

    __simt_callee__ inline ~asccomm();

    /*!
     * @brief Initialize Hcomm.
     * @param [in] buff: Not used. Pass nullptr.
     * @param [in] len: Not used. Pass 0.
     * @return 0 indicates success and -1 indicates failure.
     * @note Every posting lane calls init on its own Hcomm object. The shared UB workspace for
     *       a group batch is passed to make_batch_handle, not init.
     */
    __simt_callee__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);

    /*!
     * @brief Create an explicit batch bound to one remote registration.
     * @param [in] channel: Channel exclusively used by this batch handle.
     * @param [in] buff: Caller-owned UB storage for the shared context and publisher image.
     * @param [in] buff_len: Size of buff in bytes; reserve at least 256 bytes.
     * @note The 256-byte workspace holds a 128-byte shared context and a 128-byte publisher
     *       DWQE image. Each lane writes its request directly to the SQ, so the workspace
     *       size does not grow with the group size.
     * @note On failure this function returns a zero-valued handle (context == nullptr). The
     *       caller must check it before any batch posting, batch_commit or drain operation.
     * @note Every remote data and notify range appended through the returned handle must stay in
     *       the remote registered buffers of this channel. The caller owns this contract.
     * @note The handle keeps the group size fixed for its lifetime. Every appended request re-plans the batch
     *       width with its own layout; requests appended to one batch must use the same width, and mixing
     *       widths within a batch is unsupported. Every batch contains exactly one request per lane. A channel
     *       is exclusively owned by one live batch handle, and the non-batch posting interfaces of this class
     *       must not be used on that channel while the handle is live.
     */
    __simt_callee__ inline batch_handle<ChannelHandle> make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len);

    /*!
     * @brief Create a batch for the explicitly supplied cooperative group.
     * @param [in] group: Cooperative group whose lanes own this batch.
     * @note Pass the same group to every posting call and batch_commit for this handle.
     *       The no-group overloads are for single-lane handles. A supplied coop_group uses the
     *       collective path, including when its current size is 1.
     *       Use fixed group membership, lane ranks and size for the handle's lifetime.
     *       Every lane must pass the same valid buff and buff_len arguments.
     */
    template <typename coop_group>
    __simt_callee__ inline batch_handle<ChannelHandle> make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, const coop_group& group);

    /*!
     * @brief The task launching interface of the Write point-to-point communication operator.
     *        (task content: Write data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. len must not exceed 256 MB
     *       (256 * 1024 * 1024 bytes).
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t write_nbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t write_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG, typename coop_group>
    __simt_callee__ inline int32_t write_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group);

    /*!
     * @brief The task launching interface of the inline Write point-to-point communication operator.
     *        The source data is provided by value and carried inline in the WQE.
     * @tparam T: The value type to write.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config, inline must be enabled.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE + inline enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] value: The inline value to write.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. config must have inline enabled, and
     *       sizeof(T) must fit in the WQE inline payload area.
     */
    template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t write_value_nbi(ChannelHandle channel, __gm__ void* dst, T value);

    template <typename T, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t write_value_nbi(ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value);

    template <typename T, auto const& config = URMA_INLINE_CFG, typename coop_group>
    __simt_callee__ inline int32_t write_value_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value, const coop_group& group);

    /*!
     * @brief The task launching interface of the Read point-to-point communication operator.
     *        (task content: Read data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to read, using byte as the basic unit.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization. len must not exceed 256 MB
     *       (256 * 1024 * 1024 bytes).
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t read_nbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t read_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len);

    template <auto const& config = URMA_DEFAULT_CFG, typename coop_group>
    __simt_callee__ inline int32_t read_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group);

    /*!
     * @brief The task launching interface of the Write-with-notify point-to-point communication operator.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit.
     * @param [in] notify_addr: The remote notify address.
     * @param [in] notify_val: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     * @note len must not exceed 256 MB (256 * 1024 * 1024 bytes).
     */
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t write_with_notify_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val);

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t write_with_notify_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val);

    template <auto const& config = URMA_DEFAULT_CFG, typename coop_group>
    __simt_callee__ inline int32_t write_with_notify_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val, const coop_group& group);

    /*!
     * @brief The task launching interface of the Fetch-and-add point-to-point communication operator.
     * @tparam T: The data type of the atomic operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetch_addr: The address to store the old value before atomic add.
     * @param [in] add_val: The remote add value.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t atomic_faa_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T add_val);

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t atomic_faa_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val);

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename coop_group>
    __simt_callee__ inline int32_t atomic_faa_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val,
        const coop_group& group);

    /*!
     * @brief The task launching interface of the Compare-and-swap point-to-point communication operator.
     * @tparam T: The data type of the atomic operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetch_addr: The address to store the old value before atomic compare-and-swap.
     * @param [in] compare_val: The value to compare against the value at the destination address.
     * @param [in] swap_val: The value to swap into the destination address if comparison succeeds.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t atomic_cas_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val);

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t atomic_cas_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val);

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename coop_group>
    __simt_callee__ inline int32_t atomic_cas_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val,
        const coop_group& group);

    /*!
     * @brief Publish the SQ range prepared by the preceding batch posting call.
     * @note The overload with a coop_group argument is collective: every group lane must call it and
     *       prepare exactly one WQE.
     * @note The final item must request a CQE. If the preceding posting call fails to reserve SQ
     *       space, the caller must not call batch_commit for that batch.
     * @note If no request has been appended since the handle was created or the last commit, the
     *       commit fails instead of publishing an empty SQ range.
     */
    __simt_callee__ inline int32_t batch_commit(ubc_ctp_batch_handle& batch_handle);

    template <typename coop_group>
    __simt_callee__ inline int32_t batch_commit(ubc_ctp_batch_handle& batch_handle, const coop_group& group);

    /*!
     * @brief Block and drain comm tasks submitted on channel until finish processing.
     * @tparam pipe: Unused on SIMT.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called by a single lane, after every posting lane has returned.
     */
    template <auto pipe = 0>
    __simt_callee__ inline int32_t drain(ChannelHandle channel);

    template <auto pipe = 0>
    __simt_callee__ inline int32_t drain(ubc_ctp_batch_handle& batch_handle);

    // ===================== deprecated PascalCase compatibility aliases =====================
    // Legacy SIMT Hcomm interface spellings kept for source compatibility (remove after
    // 2027/12/31); each forwards to the corresponding asccomm* interface above.

    ASC_DEPRECATED(9.3.0, "2027/12/31", init)
    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len) { return init(buff, len); }

    ASC_DEPRECATED(9.3.0, "2027/12/31", make_batch_handle)
    __simt_callee__ inline batch_handle<ChannelHandle> MakeBatchHandle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen)
    {
        return make_batch_handle(channel, buff, buffLen);
    }

    template <typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", make_batch_handle)
    __simt_callee__ inline batch_handle<ChannelHandle> MakeBatchHandle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, const Group& group)
    {
        return make_batch_handle(channel, buff, buffLen, group);
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_nbi)
    __simt_callee__ inline int32_t WriteNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        return write_nbi<commit, config>(channel, dst, src, len);
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_nbi)
    __simt_callee__ inline int32_t
        WriteNbi(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        return write_nbi<config>(batchHandle, dst, src, len);
    }

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_nbi)
    __simt_callee__ inline int32_t WriteNbi(
        ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
    {
        return write_nbi<config>(batchHandle, dst, src, len, group);
    }

    template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_value_nbi)
    __simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value)
    {
        return write_value_nbi<T, commit, config>(channel, dst, value);
    }

    template <typename T, auto const& config = URMA_INLINE_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_value_nbi)
    __simt_callee__ inline int32_t WriteValueNbi(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, T value)
    {
        return write_value_nbi<T, config>(batchHandle, dst, value);
    }

    template <typename T, auto const& config = URMA_INLINE_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_value_nbi)
    __simt_callee__ inline int32_t
        WriteValueNbi(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, T value, const Group& group)
    {
        return write_value_nbi<T, config>(batchHandle, dst, value, group);
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", read_nbi)
    __simt_callee__ inline int32_t ReadNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        return read_nbi<commit, config>(channel, dst, src, len);
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", read_nbi)
    __simt_callee__ inline int32_t
        ReadNbi(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        return read_nbi<config>(batchHandle, dst, src, len);
    }

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", read_nbi)
    __simt_callee__ inline int32_t
        ReadNbi(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
    {
        return read_nbi<config>(batchHandle, dst, src, len, group);
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_with_notify_nbi)
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal)
    {
        return write_with_notify_nbi<commit, config>(channel, dst, src, len, notifyAddr, notifyVal);
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_with_notify_nbi)
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal)
    {
        return write_with_notify_nbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal);
    }

    template <auto const& config = URMA_DEFAULT_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", write_with_notify_nbi)
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal, const Group& group)
    {
        return write_with_notify_nbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal, group);
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_faa_nbi)
    __simt_callee__ inline int32_t AtomicFAA(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
    {
        return atomic_faa_nbi<T, commit, config>(channel, dst, fetchAddr, addVal);
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_faa_nbi)
    __simt_callee__ inline int32_t
        AtomicFAA(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
    {
        return atomic_faa_nbi<T, config>(batchHandle, dst, fetchAddr, addVal);
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_faa_nbi)
    __simt_callee__ inline int32_t AtomicFAA(
        ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal, const Group& group)
    {
        return atomic_faa_nbi<T, config>(batchHandle, dst, fetchAddr, addVal, group);
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_cas_nbi)
    __simt_callee__ inline int32_t
        AtomicCAS(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
    {
        return atomic_cas_nbi<T, commit, config>(channel, dst, fetchAddr, compareVal, swapVal);
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_cas_nbi)
    __simt_callee__ inline int32_t
        AtomicCAS(ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
    {
        return atomic_cas_nbi<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal);
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", atomic_cas_nbi)
    __simt_callee__ inline int32_t AtomicCAS(
        ubc_ctp_batch_handle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
        const Group& group)
    {
        return atomic_cas_nbi<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal, group);
    }

    ASC_DEPRECATED(9.3.0, "2027/12/31", batch_commit)
    __simt_callee__ inline int32_t BatchCommit(ubc_ctp_batch_handle& batchHandle) { return batch_commit(batchHandle); }

    template <typename Group>
    ASC_DEPRECATED(9.3.0, "2027/12/31", batch_commit)
    __simt_callee__ inline int32_t BatchCommit(ubc_ctp_batch_handle& batchHandle, const Group& group)
    {
        return batch_commit(batchHandle, group);
    }

    template <auto pipe = 0>
    ASC_DEPRECATED(9.3.0, "2027/12/31", drain)
    __simt_callee__ inline int32_t Drain(ChannelHandle channel)
    {
        return drain<pipe>(channel);
    }

    template <auto pipe = 0>
    ASC_DEPRECATED(9.3.0, "2027/12/31", drain)
    __simt_callee__ inline int32_t Drain(ubc_ctp_batch_handle& batchHandle)
    {
        return drain<pipe>(batchHandle);
    }

private:
    asccomm_impl<protocol_traits<comm_protocol>::backend> impl;
};

/*!
 * @brief Resolve the base address of a locally registered buffer on the channel.
 * @param [in] channel: The handle of the communication channel.
 * @param [in] buffer_idx: Index into the channel's local registered buffer table.
 * @return Base address of the requested local buffer.
 */
__simt_callee__ inline __gm__ uint8_t* get_local_buffer_addr(ChannelHandle channel, uint32_t buffer_idx);

/*!
 * @brief Resolve the base address of a remotely registered buffer on the channel.
 * @param [in] channel: The handle of the communication channel.
 * @param [in] buffer_idx: Index into the channel's remote registered buffer table.
 * @return Base address of the requested remote buffer.
 */
__simt_callee__ inline __gm__ uint8_t* get_remote_buffer_addr(ChannelHandle channel, uint32_t buffer_idx);

} // namespace asc::simt

#include "../../../../impl/comm_api/aicore/asccomm/impl/asccomm_simt_impl.h"

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_H
#endif

#endif // INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_SIMT_H
