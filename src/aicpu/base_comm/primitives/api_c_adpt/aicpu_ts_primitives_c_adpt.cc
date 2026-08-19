/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "thread.h"
#include "launch_context.h"

#include "ub_transport_lite_impl.h"
#include "coll_comm_aicpu_mgr.h"
#include "aicpu_indop_process.h"
#include "aicpu_ts_primitives_c_adpt.h"
#include "aicpu_ts_channel_helper.h"

#include <cmath>

using namespace hccl;
thread_local LaunchContext g_threadLaunchCtx;

bool IsBatchLaunchMode() {
    return g_threadLaunchCtx.IsBatchLaunchMode();
}

uint32_t GetSqFullTimeOut() {
    return g_threadLaunchCtx.GetSqFullTimeOut();
}

void AddThread(ThreadHandle thread) {
    g_threadLaunchCtx.AddThread(thread);
}

HcclResult HandleDispatchAllStreams()
{
    return g_threadLaunchCtx.HandleDispatchAllStreams();
}

int32_t HcommLocalCopyOnThread(ThreadHandle thread, void *dst, const void *src, uint64_t len)
{
    HCCL_INFO("[%s] START. thread[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].", __func__, thread, dst, src, len);

    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalCopy(dst, src, len), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
            __func__, thread, dst, src, len), ret);
    return HCCL_SUCCESS;
}

int32_t HcommLocalReduceOnThread(ThreadHandle thread, void *dst, const void *src, uint64_t count,
    HcommDataType dataType, HcommReduceOp reduceOp)
{
    HCCL_INFO("[%s] START. thread[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].", __func__, thread, dst, src, count, dataType, reduceOp);

    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    uint64_t len = count * SIZE_TABLE[dataType];

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalReduce(dst, src, len, dataType, reduceOp), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].", __func__, thread, dst, src, count, dataType, reduceOp), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommThreadNotifyRecordOnThread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
{
    HCCL_INFO("[%s] START. thread[0x%llx], dstThread[0x%llx], dstNotifyIdx[%u].", __func__, thread, dstThread, dstNotifyIdx);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);
    Thread *const dstThreadPtr = reinterpret_cast<Thread *>(dstThread);
    CHK_PTR_NULL(dstThreadPtr);

    LocalNotify *const notifyPtr = dstThreadPtr->GetNotify(dstNotifyIdx);
    CHK_PTR_NULL(notifyPtr);
    const uint32_t notifyId = notifyPtr->notifyId_;
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalNotifyRecord(notifyId), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], dstThread[0x%llx], dstNotifyIdx[%u].", __func__, thread, dstThread, dstNotifyIdx), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommThreadNotifyWaitOnThread(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
{
    HCCL_INFO("[%s] START. thread[0x%llx], notifyIdx[%u], timeOut[%u].", __func__, thread, notifyIdx, timeOut);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    LocalNotify *const notifyPtr = threadPtr->GetNotify(notifyIdx);
    CHK_PTR_NULL(notifyPtr);
    const uint32_t notifyId = notifyPtr->notifyId_;
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalNotifyWait(notifyId, timeOut), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], notifyIdx[%u], timeOut[%u].", __func__, thread, notifyIdx, timeOut), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommAclrtNotifyRecordOnThread(ThreadHandle thread, uint64_t dstNotifyId)
{
    HCCL_INFO("[%s] START. thread[0x%llx], dstNotifyId[%llu].", __func__, thread, dstNotifyId);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalNotifyRecord(dstNotifyId), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], dstNotifyId[%llu].", __func__, thread, dstNotifyId), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommAclrtNotifyWaitOnThread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
{
    HCCL_INFO("[%s] START. thread[0x%llx], notifyId[%llu], timeOut[%u].", __func__, thread, notifyId, timeOut);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = threadPtr->LocalNotifyWait(notifyId, timeOut), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], notifyId[%llu], timeOut[%u].", __func__, thread, notifyId, timeOut), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

HcclResult CommTaskLaunch(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    CHK_PRT_RET(threadNum < 1, HCCL_ERROR("[CommTaskLaunch]threadNum is less than 1"), HCCL_E_PARA);

    Thread *threadPtr = reinterpret_cast<Thread *>(threads[0]);
    CHK_PTR_NULL(threadPtr);

    HCCL_INFO("[%s] Running on A5.", __func__);
    for (uint32_t i = 0; i < threadNum; i++) {
        Thread *threadPtrLoop = reinterpret_cast<Thread *>(threads[i]);
        CHK_PTR_NULL(threadPtrLoop);
        HCCL_INFO("[%s] Launching task in thread[0x%llx].", __func__, threads[i]);
        EXCEPTION_CATCH(threadPtrLoop->LaunchTask(), return HCCL_E_INTERNAL);
    }
    return HCCL_SUCCESS;
}

HcclResult DispatchAllStreams(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    CHK_PRT_RET(threadNum < 1, HCCL_ERROR("[DispatchAllStreams]threadNum is less than 1"), HCCL_E_PARA);

    for (uint32_t i = 0; i < threadNum; i++) {
        Thread *threadPtrLoop = reinterpret_cast<Thread *>(threads[i]);
        CHK_PTR_NULL(threadPtrLoop);
        EXCEPTION_CATCH(threadPtrLoop->TryLaunchTask(), return HCCL_E_INTERNAL);
    }
    return HCCL_SUCCESS;
}

namespace {
// Convert hccl::HcommDataType => Hccl::DataType, hccl::HcommReduceOp => Hccl::ReduceOp

std::unordered_map<HcommDataType, Hccl::DataType> mapHcommDataTypeToA5 = {
    {HcommDataType::HCOMM_DATA_TYPE_INT8,    Hccl::DataType::INT8},
    {HcommDataType::HCOMM_DATA_TYPE_INT16,   Hccl::DataType::INT16},
    {HcommDataType::HCOMM_DATA_TYPE_INT32,   Hccl::DataType::INT32},
    {HcommDataType::HCOMM_DATA_TYPE_FP16,    Hccl::DataType::FP16},
    {HcommDataType::HCOMM_DATA_TYPE_FP32,    Hccl::DataType::FP32},
    {HcommDataType::HCOMM_DATA_TYPE_INT64,   Hccl::DataType::INT64},
    {HcommDataType::HCOMM_DATA_TYPE_UINT64,  Hccl::DataType::UINT64},
    {HcommDataType::HCOMM_DATA_TYPE_UINT8,   Hccl::DataType::UINT8},
    {HcommDataType::HCOMM_DATA_TYPE_UINT16,  Hccl::DataType::UINT16},
    {HcommDataType::HCOMM_DATA_TYPE_UINT32,  Hccl::DataType::UINT32},
    {HcommDataType::HCOMM_DATA_TYPE_FP64,    Hccl::DataType::FP64},
    {HcommDataType::HCOMM_DATA_TYPE_BFP16,   Hccl::DataType::BFP16},
    {HcommDataType::HCOMM_DATA_TYPE_INT128,  Hccl::DataType::INT128},
#ifndef OPEN_BUILD_PROJECT
    {HcommDataType::HCOMM_DATA_TYPE_HIF8,    Hccl::DataType::HIF8},
    {HcommDataType::HCOMM_DATA_TYPE_FP8E4M3, Hccl::DataType::FP8E4M3},
    {HcommDataType::HCOMM_DATA_TYPE_FP8E5M2, Hccl::DataType::FP8E5M2},
    {HcommDataType::HCOMM_DATA_TYPE_FP8E8M0, Hccl::DataType::FP8E8M0}
#endif
};

std::unordered_map<HcommReduceOp, Hccl::ReduceOp> mapHcommReduceOpToA5 = {
    {HcommReduceOp::HCOMM_REDUCE_SUM,  Hccl::ReduceOp::SUM},
    {HcommReduceOp::HCOMM_REDUCE_PROD, Hccl::ReduceOp::PROD},
    {HcommReduceOp::HCOMM_REDUCE_MAX,  Hccl::ReduceOp::MAX},
    {HcommReduceOp::HCOMM_REDUCE_MIN,  Hccl::ReduceOp::MIN}};

inline HcclResult CheckDataTypeAndReduceOp(HcommDataType dataType, HcommReduceOp reduceOp)
{
    if (mapHcommDataTypeToA5.find(dataType) == mapHcommDataTypeToA5.end()) {
        HCCL_ERROR("[%s] type[%u] is not supported.", __func__, dataType);
        return HCCL_E_PARA;
    }

    if (mapHcommReduceOpToA5.find(reduceOp) == mapHcommReduceOpToA5.end()) {
        HCCL_ERROR("[%s] op[%u] is not supported.", __func__, reduceOp);
        return HCCL_E_PARA;
    }

    return HCCL_SUCCESS;
}

} // namespace

// 设置notify wait的等待超时时间，默认单位为秒
int32_t HcommSetNotifyWaitTimeOut(float timeOut)
{
    if (std::isnan(timeOut) || timeOut < 0.0f || timeOut > static_cast<float>(UINT32_MAX)) {
        HCCL_ERROR("[%s] timeOut[%f] is invalid.", __func__, timeOut);
        return HCCL_E_PARA;
    }
    uint32_t timeOutInt = static_cast<uint32_t>(timeOut);
    HCCL_INFO("[%s] START. timeOut[%u].", __func__, timeOutInt);
    return g_threadLaunchCtx.SetNotifyWaitTimeOut(timeOutInt);
}

int32_t HcommThreadResAcquireTimeOut(float timeOut)
{
    if (std::isnan(timeOut) || timeOut < 0.0f || timeOut > static_cast<float>(UINT32_MAX)) {
        HCCL_ERROR("[%s] timeOut[%f] is invalid.", __func__, timeOut);
        return HCCL_E_PARA;
    }
    uint32_t timeOutInt = static_cast<uint32_t>(timeOut);
    HCCL_INFO("[%s] START. timeOut[%u].", __func__, timeOutInt);
    return g_threadLaunchCtx.SetSqFullTimeOut(timeOutInt);
}

int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeout(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
{
    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], localNotifyIdx[%u].",
        __func__, thread, channel, localNotifyIdx);

    uint32_t notifyWaitTimeOut;
    g_threadLaunchCtx.GetNotifyWaitTimeOut(notifyWaitTimeOut);

    HCCL_DEBUG("[%s] Using default timeout: %u s", __func__, notifyWaitTimeOut);

    int32_t ret = HcommChannelNotifyWaitOnThread(thread, channel, localNotifyIdx, notifyWaitTimeOut);
    if (ret != HCCL_SUCCESS) {
        HCCL_ERROR("[%s] HcommChannelNotifyWaitOnThread FAILED. thread[0x%llx], channel[0x%llx], localNotifyIdx[%u], ret[%d]",
            __func__, thread, channel, localNotifyIdx, ret);
        return ret;
    }

    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeout(ThreadHandle thread, uint32_t notifyIdx)
{
    HCCL_INFO("[%s] START. thread[0x%llx], notifyIdx[%u].", __func__, thread, notifyIdx);

    uint32_t notifyWaitTimeOut;
    g_threadLaunchCtx.GetNotifyWaitTimeOut(notifyWaitTimeOut);

    HCCL_DEBUG("[%s] Using default timeout: %u s", __func__, notifyWaitTimeOut);

    int32_t ret = HcommThreadNotifyWaitOnThread(thread, notifyIdx, notifyWaitTimeOut);
    if (ret != HCCL_SUCCESS) {
        HCCL_ERROR("[%s] HcommThreadNotifyWaitOnThread FAILED. thread[0x%llx], notifyIdx[%u], ret[%d]",
            __func__, thread, notifyIdx, ret);
        return ret;
    }

    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommWriteOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len);

    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));
    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(src), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(dst), len};

    EXCEPTION_CATCH(transportLitePtr->Write(locRmaBuf, rmtBuf, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommWriteReduceOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp);

    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));
    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    uint64_t len = count * SIZE_TABLE[dataType];

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(src), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(dst), len};

    ret = CheckDataTypeAndReduceOp(dataType, reduceOp);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at CheckDataTypeAndReduceOp. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    Hccl::ReduceIn reduceIn{mapHcommDataTypeToA5.at(dataType), mapHcommReduceOpToA5.at(reduceOp)};

    EXCEPTION_CATCH(transportLitePtr->WriteReduce(locRmaBuf, rmtBuf, reduceIn, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommWriteWithNotifyOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t len, uint32_t remoteNotifyIdx)
{
    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, len, remoteNotifyIdx);

    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));
    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(src), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, len, remoteNotifyIdx), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(dst), len};

    Hccl::WithNotifyIn withNotify{Hccl::TransportNotifyType::NORMAL, remoteNotifyIdx};

    EXCEPTION_CATCH(transportLitePtr->WriteWithNotify(locRmaBuf, rmtBuf, withNotify, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, len, remoteNotifyIdx), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommWriteReduceWithNotifyOnThread(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp, remoteNotifyIdx);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    uint64_t len = count * SIZE_TABLE[dataType];

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(src), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp, remoteNotifyIdx), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(dst), len};

    ret = CheckDataTypeAndReduceOp(dataType, reduceOp);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at CheckDataTypeAndReduceOp. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp, remoteNotifyIdx), ret);
    Hccl::ReduceIn reduceIn{mapHcommDataTypeToA5.at(dataType), mapHcommReduceOpToA5.at(reduceOp)};
    Hccl::WithNotifyIn withNotify{Hccl::TransportNotifyType::NORMAL, remoteNotifyIdx};

    EXCEPTION_CATCH(transportLitePtr->WriteReduceWithNotify(locRmaBuf, rmtBuf, reduceIn, withNotify, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp, remoteNotifyIdx), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(dst), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(src), len};

    EXCEPTION_CATCH(transportLitePtr->Read(locRmaBuf, rmtBuf, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].",
        __func__, thread, channel, dst, src, len), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommReadReduceOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    uint64_t len = count * SIZE_TABLE[dataType];

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    Hccl::RmaBufferLite locRmaBuf;
    HcclResult ret = transportLitePtr->BuildLocRmaBufferLite(reinterpret_cast<uintptr_t>(dst), len, locRmaBuf);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at BuildLocRmaBufferLite. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    const Hccl::Buffer rmtBuf{reinterpret_cast<uintptr_t>(src), len};

    ret = CheckDataTypeAndReduceOp(dataType, reduceOp);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL at CheckDataTypeAndReduceOp. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    Hccl::ReduceIn reduceIn{mapHcommDataTypeToA5.at(dataType), mapHcommReduceOpToA5.at(reduceOp)};

    EXCEPTION_CATCH(transportLitePtr->ReadReduce(locRmaBuf, rmtBuf, reduceIn, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], count[%llu], dataType[%d], reduceOp[%d].",
        __func__, thread, channel, dst, src, count, dataType, reduceOp), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommBatchTransferOnThread(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], transferDescNum[%u].",
        __func__, thread, channel, transferDescNum);

    CHK_PRT_RET(transferDescNum == 0,
        HCCL_ERROR("[%s] transferDescNum is 0.", __func__), HCCL_E_PARA);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);
    AddThread(thread);
    auto *const ubTransportLitePtr = reinterpret_cast<Hccl::UbTransportLiteImpl *>(channel);
    CHK_PTR_NULL(ubTransportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);
    HcclResult ret = ubTransportLitePtr->ExecuteBatchTransfer(streamLitePtr, transferDescs, transferDescNum);
    CHK_PRT_RET(ret != HCCL_SUCCESS,
        HCCL_ERROR("[%s] BatchTransferAsync failed.", __func__), ret);

    HCCL_INFO("[%s] SUCCESS. transferDescNum[%u].", __func__, transferDescNum);
    return ret;
}

int32_t HcommWriteNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HCCL_DEBUG("[%s] thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].", __func__, thread, channel, dst, src, len);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommWriteNbi(ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HCCL_DEBUG("[%s] channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].", __func__, channel, dst, src, len);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommWriteWithNotifyNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t len, uint32_t remoteNotifyIdx)
{
    HCCL_DEBUG("[%s] thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu], remoteNotifyIdx[%u].",
        __func__, thread, channel, dst, src, len, remoteNotifyIdx);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommWriteWithNotifyNbi(ChannelHandle channel, void *dst, const void *src,
    uint64_t len, uint32_t remoteNotifyIdx)
{
    HCCL_DEBUG("[%s] channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu], remoteNotifyIdx[%u].",
        __func__, channel, dst, src, len, remoteNotifyIdx);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommReadNbiOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HCCL_DEBUG("[%s] thread[0x%llx], channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].", __func__, thread, channel, dst, src, len);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommReadNbi(ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HCCL_DEBUG("[%s] channel[0x%llx], dst[0x%llx], src[0x%llx], len[%llu].", __func__, channel, dst, src, len);
    CHK_PTR_NULL(src);
    CHK_PTR_NULL(dst);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommChannelNotifyRecordOnThread(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
{
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], remoteNotifyIdx[%u].", __func__, thread, channel, remoteNotifyIdx);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);
    HCCL_INFO("channel streamlite ptr %p.", streamLitePtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(transportLitePtr->Post(remoteNotifyIdx, *streamLitePtr), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], remoteNotifyIdx[%u].", __func__, thread, channel, remoteNotifyIdx), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommChannelNotifyRecord(ChannelHandle channel, uint32_t remoteNotifyIdx)
{
    HCCL_DEBUG("[%s] channel[0x%llx], remoteNotifyIdx[%u].", __func__, channel, remoteNotifyIdx);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommChannelNotifyWaitOnThread(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_INFO("[%s] START. thread[0x%llx], channel[0x%llx], localNotifyIdx[%u], timeOut[%u].", __func__, thread, channel, localNotifyIdx, timeOut);

    AddThread(thread);

    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(transportLitePtr->WaitWithTimeout(localNotifyIdx, *streamLitePtr, timeOut), ret = HCCL_E_INTERNAL);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s] FAIL. thread[0x%llx], channel[0x%llx], localNotifyIdx[%u], timeOut[%u].", __func__, thread, channel, localNotifyIdx, timeOut), ret);
    HCCL_INFO("[%s] SUCCESS.", __func__);
    return HCCL_SUCCESS;
}

int32_t HcommChannelNotifyWait(ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    HCCL_DEBUG("[%s] channel[0x%llx], localNotifyIdx[%u], timeOut[%u].", __func__, channel, localNotifyIdx, timeOut);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommSetLaunchMode(const char *launchTag, HcommLaunchMode mode)
{
    HCCL_DEBUG("HcommSetLaunchMode launchTag[%s]", launchTag);
    return g_threadLaunchCtx.SetLaunchMode(launchTag, mode);
}


int32_t HcommBatchModeStart(const char *batchTag)
{
    return HcommSetLaunchMode(batchTag, HCOMM_LAUNCH_MODE_BATCH);
}

int32_t HcommBatchModeEnd(const char *batchTag)
{
    return HcommSetLaunchMode(batchTag, HCOMM_LAUNCH_MODE_EAGER);
}

int32_t HcommAcquireComm(const char* commId)
{
    CHK_PTR_NULL(commId);
    HCCL_INFO("[%s]comId[%s]", __func__, commId);
    CollCommAicpuMgr *hcclComm = AicpuIndopProcess::AicpuGetCommMgrbyGroup(commId);
    CHK_PRT_RET(!hcclComm, HCCL_ERROR("%s AicpuGetCommMgrbyGroup is null, commId[%s]", __func__, commId), HCCL_E_PTR);
    return HCCL_SUCCESS;
}

int32_t HcommReleaseComm(const char* commId)
{
    CHK_PTR_NULL(commId);
    HCCL_INFO("[%s]comId[%s]", __func__, commId);
    AicpuIndopProcess::AicpuReleaseCommMgrbyGroup(commId);
    return HCCL_SUCCESS;
}

int32_t HcommFenceOnThread(ThreadHandle thread)
{
    HCCL_DEBUG("[%s] thread[0x%llx].", __func__, thread);
    return HCCL_E_NOT_SUPPORT;
}

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus
int32_t HcommChannelFenceOnThread(ThreadHandle thread, ChannelHandle channel)
{
    CHK_RET(UnwrapChannelHandle(channel));

    HCCL_DEBUG("[%s] thread[0x%llx], channel[0x%llx].", __func__, thread, channel);
    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);
    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    CHK_RET(transportLitePtr->Fence());

    return HCCL_SUCCESS;
}

int32_t HcommChannelFence(ChannelHandle channel)
{
    HCCL_DEBUG("[%s] channel[0x%llx].", __func__, channel);
    return HCCL_E_NOT_SUPPORT;
}

int32_t HcommThreadJoin(ThreadHandle thread, uint32_t timeout)
{
    hccl::Thread *threadPtr = reinterpret_cast<hccl::Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    HCCL_INFO("[%s] START. thread[0x%llx].", __func__, thread);

    HCCL_INFO("[%s] Running on A5.", __func__);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);
    auto *const rtsqPtr = streamLitePtr->GetRtsq();
    CHK_PTR_NULL(rtsqPtr);

    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t sqId = streamLitePtr->GetSqId();
    EXCEPTION_CATCH(tail = rtsqPtr->QuerySqTail(), return HCCL_E_INTERNAL);
    HCCL_INFO("[%s] aicpu stream sqid[%u] tail[%u]", __func__, sqId, tail);

    u64 startUsec = GetCurAicpuTimestamp();
    u64 lastUsec = startUsec;
    constexpr uint64_t NANOSECOND_TO_SECOND = 1000000000U;
    const uint64_t kPrintSqInterval = 30U;
    do {
        EXCEPTION_CATCH(head = rtsqPtr->QuerySqHead(), return HCCL_E_INTERNAL);
        u64 curUsec = GetCurAicpuTimestamp();
        if (curUsec - startUsec > NANOSECOND_TO_SECOND * timeout) {
            HCCL_ERROR("[%s] timeout %us. curhead:%u, curtail:%u, sqId:%u",
                __func__, timeout, head, tail, sqId);
            return HCCL_E_TIMEOUT;
        }

        if (curUsec - lastUsec > NANOSECOND_TO_SECOND * kPrintSqInterval) {
            lastUsec = curUsec;
            HCCL_RUN_INFO("[%s]Current state. sqid:%d, head:%u, tail:%u",
                __func__, sqId, head, tail);
        }
    } while (head != tail);
    HCCL_INFO("[%s] SUCCESS. RTSQ's head[%u] == tail[%u].", __func__, head, tail);
    return HCCL_SUCCESS;
}

int32_t HcommChannelDrainOnThread(ThreadHandle thread, ChannelHandle channel)
{
    CHK_RET(UnwrapChannelHandle(channel));

    AddThread(thread);
    Thread *const threadPtr = reinterpret_cast<Thread *>(thread);
    CHK_PTR_NULL(threadPtr);

    auto *const transportLitePtr = reinterpret_cast<Hccl::BaseTransportLiteImpl *>(channel);
    CHK_PTR_NULL(transportLitePtr);
    auto *const streamLitePtr = static_cast<Hccl::StreamLite *>(threadPtr->GetStreamLitePtr());
    CHK_PTR_NULL(streamLitePtr);

    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(transportLitePtr->Drain(*streamLitePtr), ret = HCCL_E_INTERNAL);
    return ret;
}
#ifdef __cplusplus
}
#endif  // __cplusplus
