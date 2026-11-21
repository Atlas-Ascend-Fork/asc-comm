/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <string>
#include <vector>
#include <asccomm/asccomm_primitives.h>
#include <asccomm/asccomm_result.h>
#include "log.h"
#include "common.h"

constexpr uint64_t HCCL_MIN_SLICE_ALIGN = 128;
constexpr uint64_t UB_MAX_DATA_SIZE = 256 * 1024 * 1024;

static asccomm_result ThreadSyncBefore(const std::vector<ThreadHandle>& threads)
{
    for (uint32_t i = 1; i < threads.size(); i++) {
        CHK_RET(asccomm_thread_notify_record_on_thread(threads[0], threads[i], 0));
    }
    for (uint32_t i = 1; i < threads.size(); i++) {
        CHK_RET(asccomm_thread_notify_wait_on_thread(threads[i], 0, CUSTOM_TIMEOUT));
    }
    return ASCCOMM_SUCCESS;
}

static asccomm_result ThreadSyncAfter(const std::vector<ThreadHandle>& threads)
{
    for (uint32_t i = 1; i < threads.size(); i++) {
        CHK_RET(asccomm_thread_notify_wait_on_thread(threads[0], i - 1, CUSTOM_TIMEOUT));
    }
    for (uint32_t i = 1; i < threads.size(); i++) {
        CHK_RET(asccomm_thread_notify_record_on_thread(threads[i], threads[0], i - 1));
    }
    return ASCCOMM_SUCCESS;
}

static asccomm_result ExecOp(const OpParam& param, const AlgResourceCtx& resCtx)
{
    const uint32_t dataTypeSize = SIZE_TABLE[param.dataType];
    const uint64_t count = param.recvCount;
    const uint64_t dataSize = count * dataTypeSize;
    if (count == 0) {
        return ASCCOMM_SUCCESS;
    }
    if (param.rankSize == 1) {
        CHK_RET(asccomm_local_copy_on_thread(resCtx.threads[0], param.outputPtr, param.inputPtr, dataSize));
        return ASCCOMM_SUCCESS;
    }

    const uint64_t cclBufferSize = resCtx.cclMem.size;
    const uint64_t slotSize = cclBufferSize / param.rankSize / HCCL_MIN_SLICE_ALIGN * HCCL_MIN_SLICE_ALIGN;
    const uint64_t maxDataSizePerLoop = std::min(UB_MAX_DATA_SIZE, slotSize);
    const uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    if (maxDataCountPerLoop == 0) {
        HCCL_ERROR(
            "CCL buffer is too small for ReduceScatter, buffer size:%llu, rank size:%u",
            static_cast<unsigned long long>(cclBufferSize), param.rankSize);
        return ASCCOMM_E_INTERNAL;
    }
    for (const ChannelInfo& channel : resCtx.channels) {
        if (channel.remoteCclMem.size < slotSize * param.rankSize) {
            HCCL_ERROR(
                "Remote CCL buffer is too small for ReduceScatter, buffer size:%llu, rank size:%u",
                static_cast<unsigned long long>(channel.remoteCclMem.size), param.rankSize);
            return ASCCOMM_E_INTERNAL;
        }
    }

    const uint64_t loopCount = count / maxDataCountPerLoop + static_cast<uint64_t>(count % maxDataCountPerLoop != 0);
    void* cclBuffAddr = resCtx.cclMem.addr;
    uint64_t processedDataCount = 0;
    for (uint64_t loop = 0; loop < loopCount; loop++) {
        const uint64_t sliceCount = std::min(maxDataCountPerLoop, count - processedDataCount);
        const uint64_t sliceSize = sliceCount * dataTypeSize;
        const uint64_t selfInputOffset =
            (static_cast<uint64_t>(param.myRank) * count + processedDataCount) * dataTypeSize;
        const void* selfInputAddr =
            static_cast<const void*>(static_cast<const uint8_t*>(param.inputPtr) + selfInputOffset);
        void* selfCclAddr = static_cast<void*>(static_cast<uint8_t*>(cclBuffAddr) + slotSize * param.myRank);
        CHK_RET(asccomm_local_copy_on_thread(resCtx.threads[0], selfCclAddr, selfInputAddr, sliceSize));

        CHK_RET(ThreadSyncBefore(resCtx.threads));
        for (uint32_t channelIdx = 0; channelIdx < resCtx.channels.size(); channelIdx++) {
            const ChannelInfo& channel = resCtx.channels[channelIdx];
            const uint64_t sendOffset =
                (static_cast<uint64_t>(channel.remoteRank) * count + processedDataCount) * dataTypeSize;
            const void* sendAddr = static_cast<const void*>(static_cast<const uint8_t*>(param.inputPtr) + sendOffset);
            void* remoteCclAddr =
                static_cast<void*>(static_cast<uint8_t*>(channel.remoteCclMem.addr) + slotSize * param.myRank);

            CHK_RET(
                asccomm_channel_notify_record_on_thread(resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_ACK));
            CHK_RET(asccomm_channel_notify_wait_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_ACK, CUSTOM_TIMEOUT));
            CHK_RET(asccomm_write_on_thread(
                resCtx.threads[channelIdx], channel.handle, remoteCclAddr, sendAddr, sliceSize));
            CHK_RET(asccomm_channel_notify_record_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_DATA_SIGNAL));
            CHK_RET(asccomm_channel_notify_wait_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_DATA_SIGNAL, CUSTOM_TIMEOUT));
        }
        CHK_RET(ThreadSyncAfter(resCtx.threads));

        void* outputAddr =
            static_cast<void*>(static_cast<uint8_t*>(param.outputPtr) + processedDataCount * dataTypeSize);
        CHK_RET(asccomm_local_copy_on_thread(resCtx.threads[0], outputAddr, cclBuffAddr, sliceSize));
        for (uint32_t rank = 1; rank < param.rankSize; rank++) {
            const void* rankDataAddr =
                static_cast<const void*>(static_cast<const uint8_t*>(cclBuffAddr) + slotSize * rank);
            CHK_RET(asccomm_local_reduce_on_thread(
                resCtx.threads[0], outputAddr, rankDataAddr, sliceCount, static_cast<asccomm_data_type>(param.dataType),
                static_cast<asccomm_reduce_op>(param.reduceOp)));
        }
        processedDataCount += sliceCount;
    }
    return ASCCOMM_SUCCESS;
}
extern "C" unsigned int LaunchCustomReduceScatterAicpuKernel(OpParam* param)
{
    AlgResourceCtx resCtxDevice;
    char* ctx = static_cast<char*>(param->resCtxDevice);
    std::vector<char> seq(ctx, ctx + param->ctxSize);
    resCtxDevice.DeSerialize(seq);

    if (asccomm_batch_mode_start(param->tag) != ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR("failed start batch mode");
        return 1;
    }

    // 主thread等待Host stream的通知
    if (asccomm_thread_notify_wait_on_thread(resCtxDevice.threads[0], param->aicpuRecordCpuIdx, CUSTOM_TIMEOUT) !=
        ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR("failed to wait notify from host main stream");
        return 1;
    }

    // 执行算法编排
    if (ExecOp(*param, resCtxDevice) != ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR("orchestrate failed for op:%d", param->opType);
        return 1;
    }

    // 主thread通知Host stream
    if (asccomm_thread_notify_record_on_thread(resCtxDevice.threads[0], param->cpuThreadOnAicpu, 0) !=
        ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR("failed to record host main stream");
        return 1;
    }

    if (asccomm_batch_mode_end(param->tag) != ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR("failed end batch mode");
        return 1;
    }
    return 0;
}
