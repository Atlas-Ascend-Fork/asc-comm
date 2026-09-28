/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "asccomm_runtime_stub.h"

AicpuRuntimeStubState g_runtimeStubState{};

bool HcclCheckLogLevel(int, int)
{
    return false;
}

bool IsErrorToWarn()
{
    return false;
}

HcclResult asccomm_resolve_thread_rtsq(HcommThreadEntity *thread)
{
    g_runtimeStubState.resolveCalls.push_back(thread);
    return g_runtimeStubState.resolveResult;
}

HcclResult asccomm_rtsq_sdma_copy(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    g_runtimeStubState.copyCalls.push_back({&context, dstAddr, srcAddr, size});
    return g_runtimeStubState.copyResult;
}

HcclResult asccomm_rtsq_sdma_reduce(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size,
    const Asc::ReduceIn &reduceIn)
{
    g_runtimeStubState.reduceCalls.push_back(
        {&context, dstAddr, srcAddr, size, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.reduceResult;
}

HcclResult asccomm_rtsq_notify_record(HcommRtsqContext &context, uint32_t notifyId)
{
    g_runtimeStubState.notifyRecordCalls.push_back({&context, notifyId});
    return g_runtimeStubState.notifyRecordResult;
}

HcclResult asccomm_rtsq_notify_wait(
    HcommRtsqContext &context, uint32_t notifyId, uint32_t timeout)
{
    g_runtimeStubState.notifyWaitCalls.push_back({&context, notifyId, timeout});
    return g_runtimeStubState.notifyWaitResult;
}

HcclResult asccomm_task_launch(ThreadHandle *threads, uint32_t threadNum)
{
    g_runtimeStubState.launchThreads.assign(threads, threads + threadNum);
    return g_runtimeStubState.launchResult;
}

HcclResult asccomm_try_launch(ThreadHandle *threads, uint32_t threadNum)
{
    g_runtimeStubState.tryLaunchThreads.assign(threads, threads + threadNum);
    return g_runtimeStubState.tryLaunchResult;
}

HcclResult asccomm_write(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    g_runtimeStubState.writeCalls.push_back({&thread, &channel, localAddr, remoteAddr, len});
    return g_runtimeStubState.writeResult;
}

HcclResult asccomm_write_reduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, const Asc::ReduceIn &reduceIn)
{
    g_runtimeStubState.writeReduceCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.writeReduceResult;
}

HcclResult asccomm_write_with_notify(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.writeNotifyCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, remoteNotifyIdx});
    return g_runtimeStubState.writeNotifyResult;
}

HcclResult asccomm_write_reduce_with_notify(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, const Asc::ReduceIn &reduceIn,
    uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.writeReduceNotifyCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len,
            reduceIn.dataType, reduceIn.reduceOp, remoteNotifyIdx});
    return g_runtimeStubState.writeReduceNotifyResult;
}

HcclResult asccomm_read(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    g_runtimeStubState.readCalls.push_back({&thread, &channel, localAddr, remoteAddr, len});
    return g_runtimeStubState.readResult;
}

HcclResult asccomm_read_reduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, const Asc::ReduceIn &reduceIn)
{
    g_runtimeStubState.readReduceCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.readReduceResult;
}

HcclResult asccomm_batch_transfer(HcommThreadEntity &thread, HcommChannelEntity &channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    g_runtimeStubState.batchTransferCalls.push_back(
        {&thread, &channel, transferDescs, transferDescNum});
    return g_runtimeStubState.batchTransferResult;
}

HcclResult asccomm_notify_record(HcommThreadEntity &thread, HcommChannelEntity &channel,
    uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.channelNotifyRecordCalls.push_back({&thread, &channel, remoteNotifyIdx});
    return g_runtimeStubState.channelNotifyRecordResult;
}

HcclResult asccomm_notify_wait(HcommThreadEntity &thread, const HcommChannelEntity &channel,
    uint32_t localNotifyIdx, uint32_t timeout)
{
    g_runtimeStubState.channelNotifyWaitCalls.push_back({&thread, &channel, localNotifyIdx, timeout});
    return g_runtimeStubState.channelNotifyWaitResult;
}

HcclResult asccomm_fence(HcommChannelEntity &channel)
{
    g_runtimeStubState.fenceCalls.push_back(&channel);
    return g_runtimeStubState.fenceResult;
}

HcclResult asccomm_drain(HcommThreadEntity &thread, HcommChannelEntity &channel)
{
    g_runtimeStubState.drainCalls.push_back({&thread, &channel});
    return g_runtimeStubState.drainResult;
}
