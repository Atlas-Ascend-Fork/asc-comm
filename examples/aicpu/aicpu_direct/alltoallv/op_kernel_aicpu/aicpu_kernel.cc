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
    const uint64_t selfCount = param.sendCounts[param.myRank];
    if (selfCount > 0) {
        const void* selfInput = static_cast<const void*>(
            static_cast<const uint8_t*>(param.inputPtr) + param.sendDispls[param.myRank] * dataTypeSize);
        void* selfOutput =
            static_cast<void*>(static_cast<uint8_t*>(param.outputPtr) + param.recvDispls[param.myRank] * dataTypeSize);
        CHK_RET(asccomm_local_copy_on_thread(resCtx.threads[0], selfOutput, selfInput, selfCount * dataTypeSize));
    }
    if (param.rankSize == 1) {
        return ASCCOMM_SUCCESS;
    }

    bool hasRemoteData = false;
    for (const ChannelInfo& channel : resCtx.channels) {
        if (param.sendCounts[channel.remoteRank] > 0 || param.recvCounts[channel.remoteRank] > 0) {
            hasRemoteData = true;
            break;
        }
    }
    if (!hasRemoteData) {
        return ASCCOMM_SUCCESS;
    }

    const uint64_t cclBufferSize = resCtx.cclMem.size;
    const uint64_t slotSize = cclBufferSize / param.rankSize / HCCL_MIN_SLICE_ALIGN * HCCL_MIN_SLICE_ALIGN;
    const uint64_t maxDataSizePerLoop = std::min(UB_MAX_DATA_SIZE, slotSize);
    const uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    if (maxDataCountPerLoop == 0) {
        HCCL_ERROR(
            "CCL buffer is too small for AlltoAllV, buffer size:%llu, rank size:%u",
            static_cast<unsigned long long>(cclBufferSize), param.rankSize);
        return ASCCOMM_E_INTERNAL;
    }
    for (const ChannelInfo& channel : resCtx.channels) {
        if (channel.remoteCclMem.size < slotSize * param.rankSize) {
            HCCL_ERROR(
                "Remote CCL buffer is too small for AlltoAllV, buffer size:%llu, rank size:%u",
                static_cast<unsigned long long>(channel.remoteCclMem.size), param.rankSize);
            return ASCCOMM_E_INTERNAL;
        }
    }

    CHK_RET(ThreadSyncBefore(resCtx.threads));
    for (uint32_t channelIdx = 0; channelIdx < resCtx.channels.size(); channelIdx++) {
        const ChannelInfo& channel = resCtx.channels[channelIdx];
        const uint32_t peerRank = channel.remoteRank;
        const uint64_t sendCount = param.sendCounts[peerRank];
        const uint64_t recvCount = param.recvCounts[peerRank];
        const uint64_t pairCount = std::max(sendCount, recvCount);
        const uint64_t loopCount =
            pairCount / maxDataCountPerLoop + static_cast<uint64_t>(pairCount % maxDataCountPerLoop != 0);
        uint64_t sendProcessed = 0;
        uint64_t recvProcessed = 0;

        for (uint64_t loop = 0; loop < loopCount; loop++) {
            const uint64_t sendSliceCount = std::min(maxDataCountPerLoop, sendCount - sendProcessed);
            const uint64_t recvSliceCount = std::min(maxDataCountPerLoop, recvCount - recvProcessed);
            void* remoteCclAddr =
                static_cast<void*>(static_cast<uint8_t*>(channel.remoteCclMem.addr) + slotSize * param.myRank);

            CHK_RET(
                asccomm_channel_notify_record_on_thread(resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_ACK));
            CHK_RET(asccomm_channel_notify_wait_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_ACK, CUSTOM_TIMEOUT));
            if (sendSliceCount > 0) {
                const uint64_t sendOffset = (param.sendDispls[peerRank] + sendProcessed) * dataTypeSize;
                const void* sendAddr =
                    static_cast<const void*>(static_cast<const uint8_t*>(param.inputPtr) + sendOffset);
                CHK_RET(asccomm_write_on_thread(
                    resCtx.threads[channelIdx], channel.handle, remoteCclAddr, sendAddr,
                    sendSliceCount * dataTypeSize));
            }
            CHK_RET(asccomm_channel_notify_record_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_DATA_SIGNAL));
            CHK_RET(asccomm_channel_notify_wait_on_thread(
                resCtx.threads[channelIdx], channel.handle, NOTIFY_IDX_DATA_SIGNAL, CUSTOM_TIMEOUT));
            if (recvSliceCount > 0) {
                const void* localCclAddr =
                    static_cast<const void*>(static_cast<const uint8_t*>(resCtx.cclMem.addr) + slotSize * peerRank);
                const uint64_t recvOffset = (param.recvDispls[peerRank] + recvProcessed) * dataTypeSize;
                void* recvAddr = static_cast<void*>(static_cast<uint8_t*>(param.outputPtr) + recvOffset);
                CHK_RET(asccomm_local_copy_on_thread(
                    resCtx.threads[channelIdx], recvAddr, localCclAddr, recvSliceCount * dataTypeSize));
            }
            sendProcessed += sendSliceCount;
            recvProcessed += recvSliceCount;
        }
    }
    CHK_RET(ThreadSyncAfter(resCtx.threads));
    return ASCCOMM_SUCCESS;
}
extern "C" unsigned int LaunchCustomAlltoAllVAicpuKernel(OpParam* param)
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
