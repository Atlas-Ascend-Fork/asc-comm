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
 * \file asccomm.h
 * \brief asccomm interface
 *
 * This header provides class asccomm with the new asccomm* point-to-point communication
 * interfaces. The legacy
 * Hcomm interface names (Init, WriteNbi, ReadNbi and so on) remain available through
 * hcomm/hcomm.h. Both sets share the same underlying implementation; calling a legacy interface
 * prints an INFO-level deprecation log and forwards to the corresponding asccomm* interface.
 */
#ifndef INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_H
#define INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_H

#include "kernel_basic_intf.h"
#include "asccomm_common.h"

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_H
#endif

#include "../../../../impl/comm_api/aicore/asccomm/impl/asccomm_impl_def.h"

namespace asc {

/*!
 * @class asccomm
 * @brief This class mainly provides a series of point-to-point communication primitive interfaces,
 *        benchmarking against Huawei's point-to-point communication C++ interface,
 *        including asccomm_write_nbi、asccomm_read_nbi and so on.
 *        The typical usage of this class is as follows:
 *          1) Create asccomm object.
 *          2) Initialize communication channel.
 *          3) Launch comm tasks asynchronously through the corresponding interface,
 *             and the server starts assembling and launching comm tasks as soon as it listens.
 *          4) If commit is false when launching task, call the asccomm_commit interface to notify the execution of the
 *             corresponding comm task.
 *          5) Call the asccomm_drain interface (blocking) to wait for the server to complete the corresponding comm task.
 *             Only CQE-generating tasks can be tracked; see the asccomm_drain notes.
 * @tparam comm_protocol: The communication protocol to use. COMM_PROTOCOL_UB_CTP (implemented through URMA) is used
 *                       by default.
 */
template <CommProtocol comm_protocol = COMM_PROTOCOL_UB_CTP>
class asccomm {
public:
    /*!
     * @brief Initialize asccomm workspace.
     * @param [in] buff: The UB buffer provided by caller. Start address must be 32-byte aligned.
     * @param [in] len: The buffer length in bytes.
     * @return 0 indicates success and -1 indicates failure.
     * @note URMA uses buff as its temporary workspace. Misaligned addresses are rejected by debug-mode checks.
     */
    __aicore__ inline int32_t asccomm_init(__ubuf__ uint8_t* buff, uint32_t len);

    /*!
     * @brief Create a batch handle and bind a C-pointer workspace.
     * @tparam T: The communication channel handle type.
     * @param [in] channel: The handle of the communication channel.
     * @param [in] buff: The UB workspace provided by caller (C pointer form). Start address must be 32-byte aligned.
     * @param [in] buff_len: The workspace length in bytes.
     * @param [in] remote_addr: For ChannelHandle, an address in the remote registered-memory region to select. If it is null,
     *                         the first remote registered buffer is selected. Ignored for multi_channel_handle.
     * @param [in] local_addr: Reserved parameter. Not used in the current version.
     * @return A batch handle for comm_protocol. Returns a zero-valued invalid handle if the multi-channel handle is
     *         invalid or remote registered-memory selection fails.
     * @note For multi_channel_handle, call asccomm_get_handle_ref to select a logical channel and remote registered memory.
     *       The 64-byte task slots used by one batch must be fewer than the channel task-submission capacity, which is
     *       determined when channel resources are created on the Host.
     */
    template <typename T>
    __aicore__ inline batch_handle<T> asccomm_make_batch_handle(
        T channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr = nullptr,
        GM_ADDR local_addr = nullptr);

    /*!
     * @brief Get the batch handle reference used to add tasks for one logical channel.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: A batch handle created from ChannelHandle or multi_channel_handle.
     * @param [in] channel_index: Index in the channel descriptor array used to create multi_channel_handle. Ignored for
     *                          ChannelHandle.
     * @param [in] remote_addr: An address in the remote registered-memory region to select. If it is null, the first
     *                         remote registered buffer is selected. Ignored for ChannelHandle.
     * @return A batch handle reference for the selected logical channel and remote registered memory. For
     *         ChannelHandle, returns the input handle itself.
     * @note For ChannelHandle, remote memory remains bound as selected by asccomm_make_batch_handle. For multi_channel_handle,
     *       batch operations through the returned handle access the selected remote registered memory. Repeated calls
     *       return the same inner handle reference and update its current selection. Different requests in one batch
     *       may select different logical channels. Add each request immediately after its asccomm_get_handle_ref call; all
     *       segments of one scatter/gather request use that selected channel, and one request cannot span channels.
     */
    template <typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline batch_handle<T>& asccomm_get_handle_ref(
        T& batch_handle, uint32_t channel_index, GM_ADDR remote_addr = nullptr);

    /*!
     * @class asccomm
     * @brief The task launching interface of the Write point-to-point communication operator.
     *        (task content: Write data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled. On the RoCE path, only cqe and fence take effect.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes) on the UB_CTP path, and less than 2 GB (2 * 1024 * 1024 *
     *                  1024 bytes) on the RoCE path.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    /**
     * @brief Submit a scatter/gather Write task through a channel. Source segments are gathered in array order and
     *        written contiguously starting at dst.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);

    /*!
     * @brief Add a Write task to a batch handle.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled, so each task generates a CQE. Inline data is not supported, and cqe can
     *                 only be 0 or 1.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: The batch handle to add the task to.
     * @param [out] dst: The remote destination address in the remote registered memory selected for batch_handle.
     * @param [in] src: The local source address.
     * @param [in] len: The length of the data to write in bytes. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes) on the UB_CTP path, and less than 2 GB (2 * 1024 * 1024 *
     *                  1024 bytes) on the RoCE path.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_write_nbi(T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);

    /*!
     * @brief Add a scatter/gather Write task to a batch handle. Data is gathered from src_descs in array order and
     *        written contiguously starting at dst.
     * @tparam config: WQE control config. Inline data is not supported, and cqe can only be 0 or 1.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batch_handle: A single-channel batch handle, or the batch handle returned by asccomm_get_handle_ref for
     *                              one logical channel of a multi-channel batch.
     * @param [out] dst: The remote destination base address. Source segments are written contiguously from this
     *                   address in src_descs order.
     * @param [in] src_descs: The local source buffer descriptor array.
     * @param [in] src_num: The number of elements in src_descs.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding asccomm_get_handle_ref call selects the logical channel for this
     *       task. All source segments are sent through that channel and cannot span channels. The caller must provide
     *       a valid descriptor array, payload addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_write_nbi(
        T& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);

    /*!
     * @class asccomm
     * @brief The task launching interface of the inline Write point-to-point communication operator.
     *        The source data is provided by value and carried inline in the WQE.
     * @tparam T: The value type to write.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config, inline must be enabled.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE + inline enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] value: The inline value to write.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t asccomm_write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value);

    /*!
     * @class asccomm
     * @brief The task launching interface of the write-with-reduce point-to-point communication operator.
     *        The source data is reduced into the destination data on the remote side.
     * @tparam T: The element data type. int8_t, int16_t, int32_t, uint32_t, half, float and bfloat16_t are supported.
     * @tparam reduce_op: The reduction operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE supported as default.
     * @tparam config: WQE control config, inline data is not supported.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The remote destination address of the reduction.
     * @param [in] src: The local source address of the reduction.
     * @param [in] count: The number of elements to reduce.
     * @return 0 indicates success and -1 indicates failure.
     * @note Only the UB_CTP/URMA path supports this interface.
     */
    template <
        typename T, asccomm_urma_reduce_op reduce_op, bool commit = true, pipe_t commit_pipe = PIPE_S,
        pipe_t req_pipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_reduce_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);

    /*!
     * @class asccomm
     * @brief @brief The task launching interface of the Write-with-notify point-to-point communication operator.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @param [in] notify_addr: The remote notify address.
     * @param [in] notify_val: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);

    /**
     * @brief Submit a scatter/gather Write-with-notify task through a channel.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
        uint64_t notify_val);

    /*!
     * @brief Add a Write-with-notify task to a batch handle.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled, so each task generates a CQE. Inline data is not supported, and cqe can
     *                 only be 0 or 1.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: The batch handle to add the task to.
     * @param [out] dst: The remote destination address in the remote registered memory selected for batch_handle.
     * @param [in] src: The local source address.
     * @param [in] len: The length of the data to write in bytes. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @param [in] notify_addr: The remote notify address. It must belong to the same remote registered memory as dst.
     * @param [in] notify_val: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notify_addr, uint64_t notify_val);

    /*!
     * @brief Add a scatter/gather Write-with-notify task to a batch handle. Data is gathered from src_descs in array
     *        order and written contiguously starting at dst.
     * @tparam config: WQE control config. Inline data is not supported, and cqe can only be 0 or 1.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batch_handle: A single-channel batch handle, or the batch handle returned by asccomm_get_handle_ref for
     *                              one logical channel of a multi-channel batch.
     * @param [out] dst: The remote destination base address. Source segments are written contiguously from this
     *                   address in src_descs order.
     * @param [in] src_descs: The local source buffer descriptor array.
     * @param [in] src_num: The number of elements in src_descs.
     * @param [in] notify_addr: The remote notify address.
     * @param [in] notify_val: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding asccomm_get_handle_ref call selects the logical channel for this
     *       task. All source segments and the notification use that channel and cannot span channels. The caller must
     *       provide a valid descriptor array, payload/notify addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        T& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
        uint64_t notify_val);

    /*!
     * @class asccomm
     * @brief @brief The task launching interface of the Fetch-and-add point-to-point communication operator.
     * @tparam T: The data type of the atomic operation. Only int32_t, uint32_t, int64_t, uint64_t is supported.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetch_addr: The address to store the old value before atomic add.
     * @param [in] add_val: The remote add value.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_atomic_faa_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val);

    /*!
     * @class asccomm
     * @brief @brief The task launching interface of the Compare-and-swap point-to-point communication operator.
     * @tparam T: The data type of the atomic operation. Only int32_t, uint32_t, int64_t, uint64_t is supported.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetch_addr: The address to store the old value before atomic compare-and-swap.
     * @param [in] compare_val: The value to compare against the value at the destination address.
     * @param [in] swap_val: The value to swap into the destination address if comparison succeeds.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_atomic_cas_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val);

    /*!
     * @class asccomm
     * @brief The task launching interface of the Read point-to-point communication operator.
     *        (task content: Read data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commit_pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam req_pipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled. On the RoCE path, only cqe and fence take effect.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to read, using byte as the basic unit. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes) on the UB_CTP path, and less than 2 GB (2 * 1024 * 1024 *
     *                  1024 bytes) on the RoCE path.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    /**
     * @brief Submit a scatter/gather Read task through a channel. Data is read contiguously from src and scattered
     *        into dst_descs in array order.
     */
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(
        ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);

    /*!
     * @brief Add a Read task to a batch handle.
     * @tparam config: WQE control config. Default: odr = Relax Order(RO) with in-order completion + fence + CQE
     *                 enabled, so each task generates a CQE. Inline data is not supported, and cqe can
     *                 only be 0 or 1.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: The batch handle to add the task to.
     * @param [out] dst: The local destination address.
     * @param [in] src: The remote source address in the remote registered memory selected for batch_handle.
     * @param [in] len: The length of the data to read in bytes. Must not exceed 256 MB
     *                  (256 * 1024 * 1024 bytes) on the UB_CTP path, and less than 2 GB (2 * 1024 * 1024 *
     *                  1024 bytes) on the RoCE path.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_read_nbi(T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);

    /*!
     * @brief Add a scatter/gather Read task to a batch handle. Data is read contiguously starting at src and scattered
     *        into dst_descs in array order.
     * @tparam config: WQE control config. Inline data is not supported, and cqe can only be 0 or 1.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batch_handle: A single-channel batch handle, or the batch handle returned by asccomm_get_handle_ref for
     *                              one logical channel of a multi-channel batch.
     * @param [in] dst_descs: The local destination buffer descriptor array. The payload is scattered into these
     *                       buffers in array order.
     * @param [in] dst_num: The number of elements in dst_descs.
     * @param [in] src: The remote source base address. The total payload is read contiguously from this address.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding asccomm_get_handle_ref call selects the logical channel for this
     *       task. All destination segments receive data through that channel and cannot span channels. The caller must
     *       provide a valid descriptor array, payload addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_read_nbi(
        T& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);

    /*!
     * @class asccomm
     * @brief Informed that tasks submitted on channel can be executed.
     * @tparam pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t asccomm_commit(ChannelHandle channel);

    /*!
     * @brief Submit all tasks in the current batch of a batch handle.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: The batch handle to submit. Tasks can be added to a new batch after success.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_batch_commit(T& batch_handle);

    /*!
     * @class asccomm
     * @brief Block Aicore until all CQE-generating comm tasks submitted to the channel have completed.
     * @tparam pipe: The pipe type to use for drain, PIPE_MTE3 supported as default.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success. A non-zero value indicates failure. For COMM_PROTOCOL_UB_CTP, the underlying
     *         CQ polling error code is returned directly.
     * @note Completion status is tracked through CQEs: only tasks that generate a CQE (the cqe bit set in the
     *       wqe_entry config, which is the default) can be tracked and awaited. Tasks submitted without a CQE
     *       request are executed by the engine but generate no CQE, so their completion cannot be tracked by
     *       this interface; synchronize them through other means if their completion matters.
     */
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t asccomm_drain(ChannelHandle channel);

    /*!
     * @brief Block Aicore until all CQEs generated through a batch handle are available.
     * @tparam pipe: The pipe type to use for drain, PIPE_S supported as default.
     * @tparam T: The batch handle type.
     * @param [in,out] batch_handle: The batch handle whose generated CQEs are awaited.
     * @return 0 indicates success. A non-zero value indicates failure. For COMM_PROTOCOL_UB_CTP, 0xFF indicates a
     *         CQE polling timeout; other positive values encode status and substatus.
     * @note This overload does not require asccomm_init. It must be called after all tasks in the current batch are submitted
     *       through asccomm_batch_commit. Multiple batches may be submitted before one asccomm_drain if channel capacity permits.
     *       As with the channel overload, only tasks that generate a CQE (the cqe bit set in the wqe_entry config,
     *       which is the default) can be tracked; tasks submitted without a CQE request are not covered.
     */
    template <pipe_t pipe = PIPE_S, typename T, typename handle_traits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t asccomm_drain(T& batch_handle);

    /*!
     * @brief Acquire the cross-AI-Core lock of a channel.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note This interface is supported by both COMM_PROTOCOL_UB_CTP and COMM_PROTOCOL_ROCE on Ascend 950. It
     *       blocks until the lock is acquired and does not guarantee acquisition order among AI Cores. All
     *       accesses that update the channel state must be protected by asccomm_lock and asccomm_unlock.
     */
    __aicore__ inline int32_t asccomm_lock(ChannelHandle channel);

    /*!
     * @brief Flush the channel state and release its cross-AI-Core lock.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note This interface is supported by both COMM_PROTOCOL_UB_CTP and COMM_PROTOCOL_ROCE on Ascend 950. It
     *       must be called by the AI Core that successfully acquired the channel lock, after the last operation
     *       that updates the channel state.
     */
    __aicore__ inline int32_t asccomm_unlock(ChannelHandle channel);

private:
    asccomm_impl<protocol_traits<comm_protocol>::backend> impl;
};

} // namespace asc

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202 || \
                              __NPU_ARCH__ == 1001 || __NPU_ARCH__ == 2002 || __NPU_ARCH__ == 2201)
#include "../../../../impl/comm_api/aicore/asccomm/impl/asccomm_impl.h"
#endif

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_H
#endif

#endif // INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_H
