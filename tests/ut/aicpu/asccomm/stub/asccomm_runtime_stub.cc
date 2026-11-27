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

bool HcclCheckLogLevel(int, int) { return false; }

bool IsErrorToWarn() { return false; }

asccomm_result asccomm_resolve_thread_rtsq(HcommThreadEntity* thread)
{
    g_runtimeStubState.resolveCalls.push_back(thread);
    return g_runtimeStubState.resolveResult;
}

asccomm_result asccomm_validate_device_range(uint64_t addr, uint64_t len)
{
    g_runtimeStubState.deviceRangeQueries.push_back({addr, len});
    if (g_runtimeStubState.deviceRangeResult != 0) {
        return ASCCOMM_E_PARA;
    }
    const uint64_t base = g_runtimeStubState.deviceRange.base;
    const uint64_t size = g_runtimeStubState.deviceRange.size;
    if (base > addr || len > size || addr - base > size - len) {
        return ASCCOMM_E_PARA;
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_rtsq_sdma_copy(HcommThreadEntity& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    g_runtimeStubState.copyCalls.push_back({&context, dstAddr, srcAddr, size});
    return g_runtimeStubState.copyResult;
}

asccomm_result asccomm_rtsq_sdma_reduce(
    HcommThreadEntity& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn)
{
    g_runtimeStubState.reduceCalls.push_back({&context, dstAddr, srcAddr, size, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.reduceResult;
}

asccomm_result asccomm_rtsq_notify_record(HcommThreadEntity& context, uint32_t notifyId)
{
    g_runtimeStubState.notifyRecordCalls.push_back({&context, notifyId});
    return g_runtimeStubState.notifyRecordResult;
}

asccomm_result asccomm_rtsq_addr_notify_wait(
    HcommThreadEntity& context, uint64_t waitAddr, uint64_t valueAddr, uint8_t last)
{
    g_runtimeStubState.addrNotifyWaitCalls.push_back({&context, waitAddr, valueAddr, last});
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_rtsq_addr_notify_record(HcommThreadEntity& context, uint64_t recordAddr, uint64_t valueAddr)
{
    g_runtimeStubState.addrNotifyRecordCalls.push_back({&context, recordAddr, valueAddr, 0});
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_rtsq_notify_wait(HcommThreadEntity& context, uint32_t notifyId, uint32_t timeout)
{
    g_runtimeStubState.notifyWaitCalls.push_back({&context, notifyId, timeout});
    return g_runtimeStubState.notifyWaitResult;
}

asccomm_result asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum)
{
    g_runtimeStubState.launchThreads.assign(threads, threads + threadNum);
    return g_runtimeStubState.launchResult;
}

asccomm_result asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum)
{
    g_runtimeStubState.tryLaunchThreads.assign(threads, threads + threadNum);
    return g_runtimeStubState.tryLaunchResult;
}

asccomm_result asccomm_write(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    g_runtimeStubState.writeCalls.push_back({&thread, &channel, localAddr, remoteAddr, len});
    return g_runtimeStubState.writeResult;
}

asccomm_result asccomm_write_reduce(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    g_runtimeStubState.writeReduceCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.writeReduceResult;
}

asccomm_result asccomm_write_with_notify(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.writeNotifyCalls.push_back({&thread, &channel, localAddr, remoteAddr, len, remoteNotifyIdx});
    return g_runtimeStubState.writeNotifyResult;
}

asccomm_result asccomm_write_reduce_with_notify(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn, uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.writeReduceNotifyCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, reduceIn.dataType, reduceIn.reduceOp, remoteNotifyIdx});
    return g_runtimeStubState.writeReduceNotifyResult;
}

asccomm_result asccomm_read(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    g_runtimeStubState.readCalls.push_back({&thread, &channel, localAddr, remoteAddr, len});
    return g_runtimeStubState.readResult;
}

asccomm_result asccomm_read_reduce(
    HcommThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    g_runtimeStubState.readReduceCalls.push_back(
        {&thread, &channel, localAddr, remoteAddr, len, reduceIn.dataType, reduceIn.reduceOp});
    return g_runtimeStubState.readReduceResult;
}

asccomm_result asccomm_send(ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uint64_t len)
{
    g_runtimeStubState.sendCalls.push_back({&thread, &channel, localAddr, len});
    return g_runtimeStubState.sendResult;
}

asccomm_result asccomm_send_with_imm(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uint64_t len, uint64_t immData)
{
    g_runtimeStubState.sendWithImmCalls.push_back({&thread, &channel, localAddr, len, immData});
    return g_runtimeStubState.sendWithImmResult;
}

asccomm_result asccomm_batch_transfer(
    HcommThreadEntity& thread, ChannelEntity& channel, const asccomm_batch_transfer_desc* transferDescs,
    uint32_t transferDescNum)
{
    g_runtimeStubState.batchTransferCalls.push_back({&thread, &channel, transferDescs, transferDescNum});
    return g_runtimeStubState.batchTransferResult;
}

asccomm_result asccomm_notify_record(HcommThreadEntity& thread, ChannelEntity& channel, uint32_t remoteNotifyIdx)
{
    g_runtimeStubState.channelNotifyRecordCalls.push_back({&thread, &channel, remoteNotifyIdx});
    return g_runtimeStubState.channelNotifyRecordResult;
}

asccomm_result asccomm_notify_wait(
    HcommThreadEntity& thread, const ChannelEntity& channel, uint32_t localNotifyIdx, uint32_t timeout)
{
    g_runtimeStubState.channelNotifyWaitCalls.push_back({&thread, &channel, localNotifyIdx, timeout});
    return g_runtimeStubState.channelNotifyWaitResult;
}

asccomm_result asccomm_fence(ChannelEntity& channel)
{
    g_runtimeStubState.fenceCalls.push_back(&channel);
    return g_runtimeStubState.fenceResult;
}

asccomm_result asccomm_drain(HcommThreadEntity& thread, ChannelEntity& channel)
{
    g_runtimeStubState.drainCalls.push_back({&thread, &channel});
    return g_runtimeStubState.drainResult;
}
