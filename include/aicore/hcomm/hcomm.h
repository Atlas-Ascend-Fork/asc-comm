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
 * \file hcomm.h
 * \brief Hcomm interface
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_H

#include "kernel_basic_intf.h"
#include "hcomm_common.h"

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_H
#endif

#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_impl_def.h"

namespace AscendC {

/*!
 * @class Hcomm
 * @brief This class mainly provides a series of point-to-point communication primitive interfaces,
 *        benchmarking against Huawei's point-to-point communication C++ interface,
 *        including WriteNbi、ReadNbi and so on.
 *        The typical usage of this class is as follows:
 *          1) Create Hcomm object.
 *          2) Initialize communication channel.
 *          3) Launch comm tasks asynchronously through the corresponding interface,
 *             and the server starts assembling and launching comm tasks as soon as it listens.
 *          4) If commit is false when launching task, call the Commit interface to notify the execution of the
 *             corresponding comm task.
 *          5) Call the Drain interface (blocking) to wait for the server to complete the corresponding comm task.
 * @tparam commProtocol: The communication protocol to use. COMM_PROTOCOL_UB_CTP (implemented through URMA) is used
 *                       by default.
 */
template <CommProtocol commProtocol = COMM_PROTOCOL_UB_CTP>
class Hcomm {
public:
    /*!
     * @brief Initialize Hcomm workspace.
     * @param [in] buff: The UB buffer provided by caller.
     * @param [in] len: The buffer length in bytes.
     * @return 0 indicates success and -1 indicates failure.
     * @note URMA uses buff as its temporary workspace after 32-byte alignment.
     */
    __aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

    /*!
     * @brief Initialize Hcomm workspace using LocalTensor.
     * @tparam T: The element type of the LocalTensor.
     * @param [in] buff: The LocalTensor buffer provided by caller. Start address must be 32-byte aligned.
     * @param [in] len: The buffer length in bytes.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T>
    __aicore__ inline int32_t Init(const LocalTensor<T>& buff, uint32_t len);

    /*!
     * @brief Create a batch handle and bind a LocalTensor workspace.
     * @tparam T: The communication channel handle type.
     * @tparam U: The element type of the LocalTensor.
     * @param [in] channel: The handle of the communication channel.
     * @param [in] buff: The LocalTensor workspace used by batch operations.
     * @param [in] buffLen: The workspace length in bytes.
     * @param [in] remoteAddr: For ChannelHandle, an address in the remote registered memory to select. If it is null,
     *                         the first remote registered buffer is selected. Ignored for MultiChannelHandle.
     * @param [in] localAddr: Reserved parameter. Not used in the current version.
     * @return A batch handle for commProtocol. Returns a zero-valued invalid handle if the multi-channel handle is
     *         invalid or remote registered-memory selection fails.
     * @note For MultiChannelHandle, call GetHandleRef to select a logical channel and remote registered memory.
     *       The 64-byte task slots used by one batch must be fewer than the channel task-submission capacity, which is
     *       determined when channel resources are created on the Host.
     */
    template <typename T, typename U>
    __aicore__ inline BatchHandle<T> MakeBatchHandle(
        T channel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr = nullptr,
        GM_ADDR localAddr = nullptr);

    /*!
     * @brief Get the batch handle reference used to add tasks for one logical channel.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: A batch handle created from ChannelHandle or MultiChannelHandle.
     * @param [in] channelIndex: Index in the channel descriptor array used to create MultiChannelHandle. Ignored for
     *                          ChannelHandle.
     * @param [in] remoteAddr: An address in the remote registered-memory region to select. If it is null, the first
     *                         remote registered buffer is selected. Ignored for ChannelHandle.
     * @return A batch handle reference for the selected logical channel and remote registered memory. For
     *         ChannelHandle, returns the input handle itself.
     * @note For ChannelHandle, remote memory remains bound as selected by MakeBatchHandle. For MultiChannelHandle,
     *       batch operations through the returned handle access the selected remote registered memory. Repeated calls
     *       return the same inner handle reference and update its current selection. Different requests in one batch
     *       may select different logical channels. Add each request immediately after its GetHandleRef call; all
     *       segments of one scatter/gather request use that selected channel, and one request cannot span channels.
     */
    template <typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline BatchHandle<T>& GetHandleRef(T& batchHandle, uint32_t channelIndex, GM_ADDR remoteAddr = nullptr);

    /*!
     * @class Hcomm
     * @brief The task launching interface of the Write point-to-point communication operator.
     *        (task content: Write data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    /*!
     * @brief Add a Write task to a batch handle.
     * @tparam config: URMA task configuration. URMA_DEFAULT_CFG uses strong ordering and fence, and each task generates
     *                 a completion record. Inline mode is not supported.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: The batch handle to add the task to.
     * @param [out] dst: The remote destination address in the remote registered memory selected for batchHandle.
     * @param [in] src: The local source address.
     * @param [in] len: The length of the data to write in bytes. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t WriteNbi(T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);

    /*!
     * @brief Add a scatter/gather Write task to a batch handle. Data is gathered from srcDescs in array order and
     *        written contiguously starting at dst.
     * @tparam config: URMA task control config. Inline data is not supported.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batchHandle: A single-channel batch handle, or the batch handle returned by GetHandleRef for
     *                              one logical channel of a multi-channel batch.
     * @param [out] dst: The remote destination base address. Source segments are written contiguously from this
     *                   address in srcDescs order.
     * @param [in] srcDescs: The local source buffer descriptor array.
     * @param [in] srcNum: The number of elements in srcDescs.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding GetHandleRef call selects the logical channel for this
     *       task. All source segments are sent through that channel and cannot span channels. The caller must provide
     *       a valid descriptor array, payload addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t WriteNbi(T& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum);

    /*!
     * @class Hcomm
     * @brief The task launching interface of the inline Write point-to-point communication operator.
     *        The source data is provided by value and carried inline in the WQE.
     * @tparam T: The value type to write.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA.
     *         Default: strongly ordered + fence + CQE + inline enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] value: The inline value to write.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value);

    /*!
     * @class Hcomm
     * @brief The task launching interface of the write-with-reduce point-to-point communication operator.
     *        The source data is reduced into the destination data on the remote side.
     * @tparam T: The element data type. int8_t, int16_t, int32_t, uint32_t, half, float and bfloat16_t are supported.
     * @tparam reduceOp: The reduction operation.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The remote destination address of the reduction.
     * @param [in] src: The local source address of the reduction.
     * @param [in] count: The number of elements to reduce.
     * @return 0 indicates success and -1 indicates failure.
     * @note Only the UB_CTP/URMA path supports this interface.
     */
    template <
        typename T, HcommUrmaReduceOp reduceOp, bool commit = true, pipe_t commitPipe = PIPE_S,
        pipe_t reqPipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteReduceNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);

    /*!
     * @class Hcomm
     * @brief @brief The task launching interface of the Write-with-notify point-to-point communication operator.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to write, using byte as the basic unit. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @param [in] notifyAddr: The remote notify address.
     * @param [in] notifyVal: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal);

    /*!
     * @brief Add a Write-with-notify task to a batch handle.
     * @tparam config: URMA task configuration. URMA_DEFAULT_CFG uses strong ordering and fence, and each task generates
     *                 a completion record. Inline mode is not supported.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: The batch handle to add the task to.
     * @param [out] dst: The remote destination address in the remote registered memory selected for batchHandle.
     * @param [in] src: The local source address.
     * @param [in] len: The length of the data to write in bytes. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @param [in] notifyAddr: The remote notify address. It must belong to the same remote registered memory as dst.
     * @param [in] notifyVal: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t WriteWithNotifyNbi(
        T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notifyAddr, uint64_t notifyVal);

    /*!
     * @brief Add a scatter/gather Write-with-notify task to a batch handle. Data is gathered from srcDescs in array
     *        order and written contiguously starting at dst.
     * @tparam config: URMA task control config. Inline data is not supported.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batchHandle: A single-channel batch handle, or the batch handle returned by GetHandleRef for
     *                              one logical channel of a multi-channel batch.
     * @param [out] dst: The remote destination base address. Source segments are written contiguously from this
     *                   address in srcDescs order.
     * @param [in] srcDescs: The local source buffer descriptor array.
     * @param [in] srcNum: The number of elements in srcDescs.
     * @param [in] notifyAddr: The remote notify address.
     * @param [in] notifyVal: The remote notify value.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding GetHandleRef call selects the logical channel for this
     *       task. All source segments and the notification use that channel and cannot span channels. The caller must
     *       provide a valid descriptor array, payload/notify addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t WriteWithNotifyNbi(
        T& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum, GM_ADDR notifyAddr, uint64_t notifyVal);

    /*!
     * @class Hcomm
     * @brief @brief The task launching interface of the Fetch-and-add point-to-point communication operator.
     * @tparam T: The data type of the atomic operation. Only int32_t, uint32_t, int64_t, uint64_t is supported.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetchAddr: The address to store the old value before atomic add.
     * @param [in] addVal: The remote add value.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t AtomicFAA(ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal);

    /*!
     * @class Hcomm
     * @brief @brief The task launching interface of the Compare-and-swap point-to-point communication operator.
     * @tparam T: The data type of the atomic operation. Only int32_t, uint32_t, int64_t, uint64_t is supported.
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [out] fetchAddr: The address to store the old value before atomic compare-and-swap.
     * @param [in] compareVal: The value to compare against the value at the destination address.
     * @param [in] swapVal: The value to swap into the destination address if comparison succeeds.
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        typename T, bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t AtomicCAS(ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T compareVal, T swapVal);

    /*!
     * @class Hcomm
     * @brief The task launching interface of the Read point-to-point communication operator.
     *        (task content: Read data of length len from src to dst through the specified channel.)
     * @tparam commit: true/false true: commit the task immediately; false: do not commit immediately.
     * @tparam commitPipe: The pipe type to use for commit, PIPE_S supported as default.
     * @tparam reqPipe: The pipe type to use for req, PIPE_MTE3 supported as default.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] channel: The handle of the communication channel.
     * @param [out] dst: The destination address of the data.
     * @param [in] src: The source address of the data.
     * @param [in] len: The length of the data to read, using byte as the basic unit. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @return 0 indicates success and -1 indicates failure.
     * @note Must be called after channel initialization.
     */
    template <
        bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    /*!
     * @brief Add a Read task to a batch handle.
     * @tparam config: URMA task configuration. URMA_DEFAULT_CFG uses strong ordering and fence, and each task generates
     *                 a completion record. Inline mode is not supported.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: The batch handle to add the task to.
     * @param [out] dst: The local destination address.
     * @param [in] src: The remote source address in the remote registered memory selected for batchHandle.
     * @param [in] len: The length of the data to read in bytes. Must be less than 256 MB
     *                  (256 * 1024 * 1024 bytes).
     * @return 0 indicates success and -1 indicates failure.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t ReadNbi(T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);

    /*!
     * @brief Add a scatter/gather Read task to a batch handle. Data is read contiguously starting at src and scattered
     *        into dstDescs in array order.
     * @tparam config: URMA task control config. Inline data is not supported.
     * @tparam T: The protocol-specific batch handle type.
     * @param [in,out] batchHandle: A single-channel batch handle, or the batch handle returned by GetHandleRef for
     *                              one logical channel of a multi-channel batch.
     * @param [in] dstDescs: The local destination buffer descriptor array. The payload is scattered into these
     *                       buffers in array order.
     * @param [in] dstNum: The number of elements in dstDescs.
     * @param [in] src: The remote source base address. The total payload is read contiguously from this address.
     * @return 0 indicates success and -1 indicates failure.
     * @note In multi-channel mode, the immediately preceding GetHandleRef call selects the logical channel for this
     *       task. All destination segments receive data through that channel and cannot span channels. The caller must
     *       provide a valid descriptor array, payload addresses and lengths.
     */
    template <auto const& config = URMA_DEFAULT_CFG, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t ReadNbi(T& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src);

    /*!
     * @class Hcomm
     * @brief Informed that tasks submitted on channel can be executed.
     * @tparam pipe: The pipe type to use for commit, PIPE_S supported as default.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t Commit(ChannelHandle channel);

    /*!
     * @brief Submit all tasks in the current batch of a batch handle.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: The batch handle to submit. Tasks can be added to a new batch after success.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t BatchCommit(T& batchHandle);

    /*!
     * @class Hcomm
     * @brief Block Aicore and drain comm tasks submitted on channel until finish processing.
     * @tparam pipe: The pipe type to use for drain, PIPE_MTE3 supported as default.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success. A non-zero value indicates failure. For COMM_PROTOCOL_UB_CTP, the underlying
     *         CQ polling error code is returned directly.
     */
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Drain(ChannelHandle channel);

    /*!
     * @brief Block Aicore until all completion records generated through a batch handle are available.
     * @tparam pipe: The pipe type to use for drain, PIPE_MTE3 supported as default.
     * @tparam T: The batch handle type.
     * @param [in,out] batchHandle: The batch handle whose generated completion records are awaited.
     * @return 0 indicates success. A non-zero value indicates failure. For COMM_PROTOCOL_UB_CTP, 0xFF indicates a
     *         completion-record polling timeout; other positive values encode status and substatus.
     * @note This overload does not require Init. It must be called after all tasks in the current batch are submitted
     *       through BatchCommit. Multiple batches may be submitted before one Drain if channel capacity permits.
     */
    template <pipe_t pipe = PIPE_MTE3, typename T, typename HandleTraits<T>::ChannelType* = nullptr>
    __aicore__ inline int32_t Drain(T& batchHandle);

    /*!
     * @brief Acquire the cross-AI-Core lock of a channel.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note This interface is supported only by COMM_PROTOCOL_UB_CTP on Ascend 950. It blocks until the lock is
     *       acquired and does not guarantee acquisition order among AI Cores. All accesses that update the channel
     *       state must be protected by Lock and Unlock.
     */
    __aicore__ inline int32_t Lock(ChannelHandle channel);

    /*!
     * @brief Flush the channel state and release its cross-AI-Core lock.
     * @param [in] channel: The handle of the communication channel.
     * @return 0 indicates success and -1 indicates failure.
     * @note This interface is supported only by COMM_PROTOCOL_UB_CTP on Ascend 950. It must be called by the AI Core
     *       that successfully acquired the channel lock, after the last operation that updates the channel state.
     */
    __aicore__ inline int32_t Unlock(ChannelHandle channel);

private:
    HcommImpl<commProtocol> impl_;
};
} // namespace AscendC

#if defined(__NPU_ARCH__) && \
    (__NPU_ARCH__ == 3510 || __NPU_ARCH__ == 1001 || __NPU_ARCH__ == 2002 || __NPU_ARCH__ == 2201)
#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_impl.h"
#endif

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_H
#endif

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_H
