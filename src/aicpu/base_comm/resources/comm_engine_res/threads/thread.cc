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
#include "sal_pub.h"
#include "comm_engine_utils.h"
#include "adapter_rts_common.h"

namespace hccl {

HcclResult Thread::AddThreadHandleToMap(CommEngine commEngine, ThreadHandle threadHandle)
{
    if (threadHandleMap_.find(commEngine) != threadHandleMap_.end() && threadHandleMap_[commEngine] != threadHandle) {
        HCCL_ERROR("[Thread][%s]Mapping already exists:commEngine[%s], threadHandle[%lu], new threadHandle[%lu]",
                   __func__, GetEnumToString(GetCommEngineStatusStrMap(), commEngine).c_str(),
                   threadHandleMap_[commEngine], threadHandle);
    }

    threadHandleMap_[commEngine] = threadHandle;
    return HCCL_SUCCESS;
}

Thread *Thread::FindThreadByCommEngine(CommEngine commEngine)
{
    if (threadHandleMap_.find(commEngine) != threadHandleMap_.end()) {
        return reinterpret_cast<Thread *>(threadHandleMap_[commEngine]);
    }

    return nullptr;
}

#ifndef CCL_KERNEL_AICPU
HcclResult Thread::ReportAicpuNotifyWaitTask(u64 notifyId, u64 beginTime, u32 taskId, u32 sqId) const
{
    if (!IsReportTask()) {
        return HCCL_SUCCESS;
    }
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_NOTIFY_WAIT;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.Notify.notifyID = notifyId;
    taskParam.taskPara.Notify.value    = 1;
    taskParam.endTime                = ProfGetCurCpuTimestamp();
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(sqId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] sqId[%u], taskId[%u], notifyId[%llu], %s", __func__, sqId, taskId,
        notifyId, taskParam.Describe().c_str());
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportHostNotifyWaitTask(u64 notifyId, u64 beginTime, bool isMaster) const
{
#ifndef CCL_KERNEL_AICPU
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_NOTIFY_WAIT;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.Notify.notifyID = notifyId;
    taskParam.taskPara.Notify.value    = 1;
    taskParam.isMaster = isMaster;
    u32 taskId = 0;
    u32 streamId = 0;
    hrtGetTaskIdAndStreamID(taskId, streamId);
    taskParam.endTime                = Hccl::DlProfFunction::GetInstance().dlMsprofSysCycleTime();
    HCCL_INFO("[ReportHostNotifyWaitTask] time is %llu", taskParam.endTime);
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(streamId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] streamId[%u], taskId[%u], notifyId[%llu], %s", __func__, streamId, taskId,
        notifyId, taskParam.Describe().c_str());
#endif
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportAicpuNotifyRecordTask(u64 notifyId, u64 beginTime, u32 taskId, u32 sqId) const
{
    if (!IsReportTask()) {
        return HCCL_SUCCESS;
    }
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_NOTIFY_RECORD;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.Notify.notifyID = notifyId;
    taskParam.taskPara.Notify.value    = 1;
    taskParam.endTime  = ProfGetCurCpuTimestamp();
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(sqId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] sqId[%u], taskId[%u], notifyId[%llu], %s", __func__, sqId, taskId,
        notifyId, taskParam.Describe().c_str());
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportHostNotifyRecordTask(u64 notifyId, u64 beginTime, bool isMaster) const
{
#ifndef CCL_KERNEL_AICPU
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_NOTIFY_RECORD;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.Notify.notifyID = notifyId;
    taskParam.taskPara.Notify.value    = 1;
    taskParam.isMaster = isMaster;
    u32 taskId = 0;
    u32 streamId = 0;
    hrtGetTaskIdAndStreamID(taskId, streamId);
    taskParam.endTime                = Hccl::DlProfFunction::GetInstance().dlMsprofSysCycleTime();
    HCCL_INFO("[ReportHostNotifyRecordTask] time is %llu", taskParam.endTime);
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(streamId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] streamId[%u], taskId[%u], notifyId[%llu], %s", __func__, streamId, taskId,
        notifyId, taskParam.Describe().c_str());
#endif
    return HCCL_SUCCESS;
}


HcclResult Thread::ReportHostLocalCopyTask(void *dst, const void *src, uint64_t sizeByte, u64 beginTime, bool isMaster) const
{
#ifndef CCL_KERNEL_AICPU
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_SDMA;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.DMA.src      = src;
    taskParam.taskPara.DMA.dst      = dst;
    taskParam.taskPara.DMA.size     = sizeByte;
    taskParam.taskPara.DMA.notifyID = INVALID_U64;
    taskParam.taskPara.DMA.linkType = Hccl::DfxLinkType::ONCHIP;
    taskParam.taskPara.DMA.dmaOp    = Hccl::DmaOp::HCCL_DMA_READ;

    u32 taskId = 0;
    u32 streamId = 0;
    hrtGetTaskIdAndStreamID(taskId, streamId);
    taskParam.endTime  = Hccl::DlProfFunction::GetInstance().dlMsprofSysCycleTime();
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(streamId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] streamId[%u], taskId[%u], src[%p], dst[%p], len[%llu] %s", __func__, streamId, taskId,
        src, dst, sizeByte, taskParam.Describe().c_str());
#endif
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportAicpuLocalCopyTask(void *dst, const void *src, uint64_t sizeByte, u64 beginTime, u32 taskId,u32 sqId) const
{
    if (!IsReportTask()) {
        return HCCL_SUCCESS;
    }
    Hccl::TaskParam taskParam{};
    taskParam.taskType              = Hccl::TaskParamType::TASK_SDMA;
    taskParam.beginTime             = beginTime;
    taskParam.taskPara.DMA.src      = src;
    taskParam.taskPara.DMA.dst      = dst;
    taskParam.taskPara.DMA.size     = sizeByte;
    taskParam.taskPara.DMA.notifyID = INVALID_U64;
    taskParam.taskPara.DMA.linkType = Hccl::DfxLinkType::ONCHIP;
    taskParam.taskPara.DMA.dmaOp    = Hccl::DmaOp::HCCL_DMA_READ;
    taskParam.endTime  = ProfGetCurCpuTimestamp();
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(sqId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] sqId[%u], taskId[%u], src[%p], dst[%p], len[%llu] %s", __func__, sqId, taskId,
        src, dst, sizeByte, taskParam.Describe().c_str());
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportAicpuLocalReduceTask(void *dst, const void *src, uint64_t sizeByte, HcommDataType dataType,
    HcommReduceOp reduceOp, u64 beginTime, u32 taskId,u32 sqId) const
{
    if (!IsReportTask()) {
        return HCCL_SUCCESS;
    }
    Hccl::TaskParam taskParam{};
    taskParam.taskType = Hccl::TaskParamType::TASK_REDUCE_INLINE;
    taskParam.beginTime = beginTime;
    taskParam.taskPara.Reduce.src = src;
    taskParam.taskPara.Reduce.dst = dst;
    taskParam.taskPara.Reduce.size = sizeByte;
    taskParam.taskPara.Reduce.notifyID = INVALID_U64;
    taskParam.taskPara.Reduce.linkType = Hccl::DfxLinkType::ONCHIP;
    taskParam.taskPara.Reduce.dataType = static_cast<HcclDataType>(dataType);
    taskParam.taskPara.Reduce.reduceOp = static_cast<HcclReduceOp>(reduceOp);
    CHK_PTR_NULL(callback_);
    CHK_RET(callback_(sqId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] sqId[%u], taskId[%u], src[%p], dst[%p], len[%llu], dataType[%d], reduceOp[%d], %s",
        __func__, sqId, taskId, src, dst, sizeByte, dataType, reduceOp, taskParam.Describe().c_str());
    return HCCL_SUCCESS;
}

HcclResult Thread::ReportHostLocalReduceTask(void *dst, const void *src, uint64_t sizeByte, HcommDataType dataType,
    HcommReduceOp reduceOp, u64 beginTime, bool isMaster) const
{
#ifndef CCL_KERNEL_AICPU
    Hccl::TaskParam taskParam{};
    taskParam.taskType                 = Hccl::TaskParamType::TASK_REDUCE_INLINE;
    taskParam.beginTime                = beginTime;
    taskParam.taskPara.Reduce.src      = src;
    taskParam.taskPara.Reduce.dst      = dst;
    taskParam.taskPara.Reduce.size     = sizeByte;
    taskParam.taskPara.Reduce.notifyID = INVALID_U64;
    taskParam.taskPara.Reduce.linkType = Hccl::DfxLinkType::ONCHIP;
    taskParam.taskPara.Reduce.dataType = static_cast<HcclDataType>(dataType);
    taskParam.taskPara.Reduce.reduceOp = static_cast<HcclReduceOp>(reduceOp);
    taskParam.isMaster = isMaster;
    u32 taskId = 0;
    u32 streamId = 0;
    hrtGetTaskIdAndStreamID(taskId, streamId);
    taskParam.endTime  = Hccl::DlProfFunction::GetInstance().dlMsprofSysCycleTime();

    CHK_RET(callback_(streamId, taskId, taskParam, INVALID_U64));
    HCCL_INFO("[Thread][%s] streamId[%u], taskId[%u], src[%p], dst[%p], len[%llu], dataType[%d], reduceOp[%d] %s",
        __func__, streamId, taskId, src, dst, sizeByte, dataType, reduceOp, taskParam.Describe().c_str());
#endif
    return HCCL_SUCCESS;
}

bool Thread::IsReportTask() const
{
    return true;
}
#endif

}  // namespace hccl
