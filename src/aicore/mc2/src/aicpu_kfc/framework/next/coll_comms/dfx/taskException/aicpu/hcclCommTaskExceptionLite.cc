/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "hcclCommTaskExceptionLite.h"
#include "aicpu_indop_process.h"
#include "stream_lite.h"
#include "global_mirror_tasks.h"
#include "task_scheduler_error.h"
#include "task_struct_v2.h"
#include "dlhal_function_v2.h"
#include "read_write_lock.h"

namespace hcomm {
constexpr u32 RT_SDMA_COMPERR = 0x9; // A3 sdma error类型为0x9时，表示写拷贝发生超时代答，或者数据搬移时地址译码错误
constexpr u32 RT_SDMA_COMPDATAERR = 0xa; // A3 sdma error类型为0xa时，表示读拷贝发生超时代答，或者读HBM返回ERROR
constexpr u32 RT_SDMA_DATAERR = 0x8; // A3 sdma error类型为0x8时，表示读HBM返回ERROR
constexpr u32 RT_UB_LOCAL_OPERATIOINERR = 0x2; // A5 ub error类型为0x2时，表示UB本端返回ERROR
constexpr u32 RT_UB_REMOTE_OPERATIOINERR = 0x3; // A5 ub error类型为0x3时，表示UB远端返回ERROR

constexpr uint32_t TASK_CONTEXT_SIZE = 50; // task 执行失败时打印谦虚task信息的数量
constexpr uint32_t TASK_CONTEXT_INFO_SIZE = LOG_TMPBUF_SIZE - 50; // task 执行失败时打印前序task信息的长度限制
constexpr u32 MAX_NAME_LEN = 64;

HcclCommTaskExceptionLite &HcclCommTaskExceptionLite::GetInstance()
{
    static HcclCommTaskExceptionLite instance; // aicpu侧一个dev一个进程，不需要按dev区分单例对象
    return instance;
}

HcclCommTaskExceptionLite::HcclCommTaskExceptionLite()
{

}

HcclCommTaskExceptionLite::~HcclCommTaskExceptionLite()
{
    initFlag_ = false;
}

void HcclCommTaskExceptionLite::Init(u32 devId)
{
    CHK_PRT_RET(initFlag_ == true, HCCL_DEBUG("%s has been initialized", __func__),);
    initFlag_ = true;
    devId_ = devId;
    HCCL_INFO("[%s]success, devId_[%u]", __func__, devId_);
}

void HcclCommTaskExceptionLite::Call()
{
    if (stopCall_ == true) {
        return;
    }

    HcclResult ret = HandleExceptionCqe();
    if (ret != HCCL_SUCCESS) {
        stopCall_ = true;
        HCCL_ERROR("[%s]HandleExceptionCqe fail, set stopCall_[%d]", __func__, stopCall_); // 函数调用失败，停止调用避免刷屏
    }
}

HcclResult HcclCommTaskExceptionLite::HandleExceptionCqe()
{
    ReadWriteLockBase &commAicpuMapMutex = AicpuIndopProcess::AicpuGetCommMutex();
    ReadWriteLock rwlock(commAicpuMapMutex);
    rwlock.readLock();
    std::vector<std::pair<std::string, CollCommAicpuMgr *>> aicpuCommInfo;
    CHK_RET(AicpuIndopProcess::AicpuGetCommAll(aicpuCommInfo));

    for (auto &commInfo : aicpuCommInfo) {
        CollCommAicpu *aicpuComm = commInfo.second->GetCollCommAicpu();
        CHK_PTR_NULL(aicpuComm);

        if (aicpuComm->GetCommmStatus() != HcclCommStatus::HCCL_COMM_STATUS_READY) {
            continue;
        }

        const std::vector<std::shared_ptr<hccl::Thread>> threads = aicpuComm->GetAllThread();
        for (auto thread : threads) {
            rtLogicCqReport_t cqeException;
            dfx::CqeStatus cqeStatus = dfx::CqeStatus::kDefault;
            Hccl::StreamLite *streamLite = static_cast<Hccl::StreamLite *>(thread->GetStreamLitePtr());
            CHK_PTR_NULL(streamLite);

            HcclResult ret = GetThreadCqe(thread.get(), cqeException, cqeStatus);
            CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s]GetThreadCqe fail, aicpuComm[%s], streamId[%u]",
                __func__, aicpuComm->GetIdentifier().c_str(), streamLite->GetId()), ret);

            if (cqeStatus != dfx::CqeStatus::kDefault) {
                ret = ProcessCqe(aicpuComm, cqeException);
                CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[%s]ProcessCqe fail, aicpuComm[%s], streamId[%u], "
                    "cqeStatus[%d]", __func__, aicpuComm->GetIdentifier().c_str(), streamLite->GetId(), cqeStatus), ret);
            }
        }
    }
    rwlock.readUnlock();
    return HCCL_SUCCESS;
}

HcclResult HcclCommTaskExceptionLite::GetThreadCqe(hccl::Thread* thread, rtLogicCqReport_t &cqeException,
    dfx::CqeStatus &cqeStatus)
{
    CHK_SMART_PTR_NULL(thread);
    Hccl::StreamLite *streamLite = static_cast<Hccl::StreamLite *>(thread->GetStreamLitePtr());
    CHK_PTR_NULL(streamLite);

    constexpr u32 reportSize = MAX_REPORT_CNT;
    rtLogicCqReport_t streamReport[reportSize];
    
    CqeQueryInput cqeQueryInput;
    cqeQueryInput.devId = devId_;
    cqeQueryInput.streamId = streamLite->GetId();
    cqeQueryInput.sqId = streamLite->GetSqId();
    cqeQueryInput.cqId = streamLite->GetCqId();
    cqeQueryInput.type = static_cast<uint32_t>(DRV_LOGIC_TYPE);
    cqeQueryInput.cqeAddr = reinterpret_cast<uint8_t *>(streamReport);
    
    cqeStatus = CqReportRecv(cqeQueryInput, cqeException);
    if (cqeStatus == dfx::CqeStatus::kCqeInnerError) {
        HCCL_ERROR("[%s]CqReportRecv fail, CqeQueryInput:%s", __func__, cqeQueryInput.ToString().c_str());
        return HCCL_E_INTERNAL;
    }
    return HCCL_SUCCESS;
}

HcclResult HcclCommTaskExceptionLite::ProcessCqe(CollCommAicpu *aicpuComm, const rtLogicCqReport_t &exceptionInfo)
{
    CHK_PTR_NULL(aicpuComm);

    // exceptionInfo->taskId和exceptionInfo->streamId拼成sqeId
    const u32 sqeId = static_cast<uint32_t>(exceptionInfo.taskId << 16) | static_cast<uint32_t>(exceptionInfo.streamId);
    HCCL_INFO("[%s]group[%s], sqeId[0x%x], taskId[%u], streamId[%u].",
        __func__, aicpuComm->GetIdentifier().c_str(), sqeId, exceptionInfo.taskId, exceptionInfo.streamId);
    const auto curTask = Hccl::GlobalMirrorTasks::Instance().GetTaskInfo(devId_, exceptionInfo.sqId, sqeId);
    if (curTask == nullptr) {
        // 未找到异常对应的TaskInfo
        HCCL_ERROR("[%s]Exception task not found. devId_[%u], streamId(sqId)[%u], taskId(sqeId)[%u].",
            __func__, devId_, exceptionInfo.sqId, sqeId);
        return HCCL_E_PARA;
    }

    // 每个通信域仅首次上报（N秒快恢时重置）
    if (!aicpuComm->IsErrorReported()) {
        // 1) errorMessage上报
        Hccl::ErrorMessageReport errMsgInfo{};
        CHK_RET(GenerateErrorMessageReport(aicpuComm, *curTask, exceptionInfo, errMsgInfo));
        CHK_RET(aicpuComm->SendErrorMessageReportToHost(errMsgInfo));

        // 2) send mbox to tsfw
        if (curTask->dfxOpInfo_ == nullptr) {
            HCCL_ERROR("[%s]dfxOpInfo is nullptr. devId_[%u], streamId(sqId)[%u], taskId(sqeId)[%u].",
                __func__, devId_, exceptionInfo.sqId, sqeId);
        } else {
            u32 notifyId = curTask->dfxOpInfo_->cpuWaitAicpuNotifyId_;
            CHK_RET(SendTaskExceptionByMBox(notifyId, 0, exceptionInfo));
            aicpuComm->SetErrorReported(true);
        }
    }

    // 1. 打印task信息
    HCCL_ERROR("[TaskException][AICPU]base information is %s, %s",
        GetBaseInfo(*curTask).c_str(), curTask->GetParaInfo().c_str());
    // 2. UB任务打印EID信息
    PrintEid(*curTask);
    // 3. 打印group信息
    HCCL_ERROR("[TaskException][AICPU]group information is %s.", GetGroupInfo(*curTask).c_str());
    // 4. 打印算子信息和task序列
    if (curTask->taskParam_.taskType != Hccl::TaskParamType::TASK_NOTIFY_WAIT) { // 非notify场景，仅打印算子信息
        HCCL_ERROR("[TaskException][AICPU]opData information is %s.", GetOpDataInfo(*curTask).c_str());
    } else {
        CHK_RET(PrintTaskContextInfo(exceptionInfo.sqId, sqeId)); // notify场景打印算子信息和task序列
    }
    return HCCL_SUCCESS;
}

std::string HcclCommTaskExceptionLite::GetBaseInfo(const Hccl::TaskInfo& taskInfo)
{
    u32 opIndex = (taskInfo.dfxOpInfo_ == nullptr) ? INVALID_UINT : taskInfo.dfxOpInfo_->opIndex_;
    return Hccl::StringFormat("streamID(sqId):[%u], taskID(sqeId):[%u], taskType:[%s], opIndex[%u]",
        taskInfo.streamId_, taskInfo.taskId_, taskInfo.taskParam_.taskType.Describe().c_str(), opIndex);
}

HcclResult HcclCommTaskExceptionLite::GenerateErrorMessageReport(CollCommAicpu *aicpuComm,
    const Hccl::TaskInfo& taskInfo, const rtLogicCqReport_t &exceptionInfo, Hccl::ErrorMessageReport &errMsgInfo)
{
    // 获取需要上报的关键信息
    errMsgInfo.remoteUserRank = taskInfo.remoteRank_;
    errMsgInfo.streamId = taskInfo.streamId_;
    errMsgInfo.taskId = taskInfo.taskId_;
    errMsgInfo.rankId = aicpuComm->GetTopoInfo().userRank;
    errMsgInfo.rankSize = aicpuComm->GetTopoInfo().userRankSize;
    strcpy_s(errMsgInfo.algType, MAX_NAME_LEN, taskInfo.dfxOpInfo_ == nullptr ? "MESH" :
                                                                                taskInfo.dfxOpInfo_->algType_.c_str());
    errMsgInfo.opIndex = taskInfo.dfxOpInfo_ == nullptr ? 0 : taskInfo.dfxOpInfo_->opIndex_;
    errMsgInfo.opType = taskInfo.dfxOpInfo_->op_.opType;
    errMsgInfo.count = taskInfo.dfxOpInfo_->op_.dataCount;
    errMsgInfo.dataType = taskInfo.dfxOpInfo_->op_.dataType;
    errMsgInfo.srcAddr = static_cast<u64>(taskInfo.dfxOpInfo_->op_.inputMem->GetAddr());
    errMsgInfo.dstAddr = static_cast<u64>(taskInfo.dfxOpInfo_->op_.outputMem->GetAddr());
    errMsgInfo.taskType = taskInfo.taskParam_.taskType;

    if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_NOTIFY_WAIT) {
        errMsgInfo.notifyId = taskInfo.taskParam_.taskPara.Notify.notifyID;
        errMsgInfo.notifyValue = taskInfo.taskParam_.taskPara.Notify.value;
    } else if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_REDUCE_INLINE
        || taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_REDUCE_WITH_NOTIFY) {
        errMsgInfo.notifyId = taskInfo.taskParam_.taskPara.Reduce.notifyID;
        errMsgInfo.notifyValue = taskInfo.taskParam_.taskPara.Reduce.notifyValue;
    } else if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_INLINE_WRITE
        || taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_WITH_NOTIFY) {
        errMsgInfo.notifyId = taskInfo.taskParam_.taskPara.DMA.notifyID;
        errMsgInfo.notifyValue = taskInfo.taskParam_.taskPara.DMA.notifyValue;
    }

    if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_REDUCE_INLINE
        || taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_REDUCE_WITH_NOTIFY
        || taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_REDUCE_INLINE) {
        errMsgInfo.reduceType = taskInfo.taskParam_.taskPara.Reduce.reduceOp;
    }

    CHK_SAFETY_FUNC_RET(memcpy_s(errMsgInfo.tag, sizeof(errMsgInfo.tag),
        taskInfo.dfxOpInfo_->algTag_.c_str(), taskInfo.dfxOpInfo_->algTag_.size()));
    CHK_SAFETY_FUNC_RET(memcpy_s(errMsgInfo.group, sizeof(errMsgInfo.group),
        aicpuComm->GetIdentifier().c_str(), aicpuComm->GetIdentifier().size()));

    GetErrMsgInfo(taskInfo, errMsgInfo, exceptionInfo);

    errMsgInfo.rtCqErrorType = exceptionInfo.errorType;
    errMsgInfo.rtCqErrorCode = exceptionInfo.errorCode;
    return HCCL_SUCCESS;
}

void HcclCommTaskExceptionLite::GetErrMsgInfo(const Hccl::TaskInfo& taskInfo, Hccl::ErrorMessageReport &errMsgInfo,
    const rtLogicCqReport_t &exceptionInfo)
{
    if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_WITH_NOTIFY ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_INLINE_WRITE ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB) {
        errMsgInfo.locEid = taskInfo.taskParam_.taskPara.DMA.locEid;
        errMsgInfo.rmtEid = taskInfo.taskParam_.taskPara.DMA.rmtEid;
        errMsgInfo.ubCqeStatus = exceptionInfo.errorCode & 0xFF;
        errMsgInfo.linkType = taskInfo.taskParam_.taskPara.DMA.linkType;
 	    errMsgInfo.size = taskInfo.taskParam_.taskPara.DMA.size;
        errMsgInfo.taskSrcAddr = reinterpret_cast<u64>(taskInfo.taskParam_.taskPara.DMA.src);
        errMsgInfo.taskDstAddr = reinterpret_cast<u64>(taskInfo.taskParam_.taskPara.DMA.dst);
    } else if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_REDUCE_INLINE ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_REDUCE_WITH_NOTIFY) {
        errMsgInfo.locEid = taskInfo.taskParam_.taskPara.Reduce.locEid;
        errMsgInfo.rmtEid = taskInfo.taskParam_.taskPara.Reduce.rmtEid;
        errMsgInfo.ubCqeStatus = exceptionInfo.errorCode & 0xFF;
        errMsgInfo.linkType = taskInfo.taskParam_.taskPara.Reduce.linkType;
 	    errMsgInfo.size = taskInfo.taskParam_.taskPara.Reduce.size;
        errMsgInfo.taskSrcAddr = reinterpret_cast<u64>(taskInfo.taskParam_.taskPara.Reduce.src);
        errMsgInfo.taskDstAddr = reinterpret_cast<u64>(taskInfo.taskParam_.taskPara.Reduce.dst);
    }

    errMsgInfo.rtCqErrorType = exceptionInfo.errorType;
    errMsgInfo.rtCqErrorCode = exceptionInfo.errorCode;
}

HcclResult HcclCommTaskExceptionLite::SendTaskExceptionByMBox(const u32 notifyId, const u32 tsId,
    const rtLogicCqReport_t &exceptionInfo)
{
    ts_aicpu_msg_info_t aicpuSqe = {};
    u32 hostpid = 0;
    u32 vfId = 0;
    int pid = getpid();
    HCCL_INFO("[%s]getpid[%d]", __func__, pid);
    // 调整drvQueryProcessHostPid获取pid和vf_id的值
    CHK_RET(HrtHalDrvQueryProcessHostPid(pid, nullptr, &vfId, &hostpid, nullptr));

    aicpuSqe.pid = hostpid;
    aicpuSqe.cmd_type = TS_AICPU_RECORD;
    aicpuSqe.vf_id = vfId;
    aicpuSqe.tid = 0U;  // notify is no need tid
    aicpuSqe.u.aicpu_record.record_type = AICPU_MSG_NOTIFY_RECORD_V2;
    aicpuSqe.u.aicpu_record.record_id = notifyId;
    aicpuSqe.ts_id = static_cast<uint8_t>(tsId);
    aicpuSqe.u.aicpu_record.fault_task_id = 0xffffffff;

    const uint8_t ubErrorType = 1; // ub类型为1
    if (exceptionInfo.errorType == ubErrorType) {
        aicpuSqe.u.aicpu_record.ret_code = SwitchUBCqeErrCodeToTsErrCode(exceptionInfo.errorCode & 0xFF);
    } else {
        aicpuSqe.u.aicpu_record.ret_code = SwitchSdmaCqeErrCodeToTsErrCode(exceptionInfo.errorCode);
    }

    struct event_summary event;
    event.dst_engine = TS_CPU;
    event.policy = ONLY;
    event.pid = 0;
    event.grp_id = 0;
    event.event_id = EVENT_TS_CTRL_MSG;
    event.subevent_id = 0U;
    event.msg_len = static_cast<uint32_t>(sizeof(ts_aicpu_msg_info_t));
    event.msg = reinterpret_cast<char_t *>(&aicpuSqe);
    drvError_t ret = Hccl::DlHalFunctionV2::GetInstance().dlHalEschedSubmitEvent(devId_, &event);
    if (ret != DRV_ERROR_NONE) {
        HCCL_ERROR("[%s]dlHalEschedSubmitEvent failed, ret=%d, notifyId=%u, hostpid=%u, vfId=%u, tsId=%u",
            __func__, ret, notifyId, hostpid, vfId, tsId);
        return HCCL_E_DRV;
    }
    HCCL_RUN_INFO("[%s]fininsh, notifyId=%u, hostpid=%u, vfId=%u, tsId=%u, errorType=%u, errorCode=%u, ret_code=%u",
        __func__, notifyId, hostpid, vfId, tsId, exceptionInfo.errorType, exceptionInfo.errorCode,
        aicpuSqe.u.aicpu_record.ret_code);
    return HCCL_SUCCESS;
}

// 把UB类错误码转换成Ts对应的错误码
uint16_t HcclCommTaskExceptionLite::SwitchUBCqeErrCodeToTsErrCode(u32 cqeErrCode) {
    switch (cqeErrCode) {
        case RT_UB_LOCAL_OPERATIOINERR:
            return TS_ERROR_LOCAL_MEM_ERROR;
        case RT_UB_REMOTE_OPERATIOINERR:
            return TS_ERROR_REMOTE_MEM_ERROR;
        default:
            return TS_ERROR_HCCL_OTHER_ERROR;
    }
}

// 把SDMA类错误码转换成Ts对应的错误码
uint16_t HcclCommTaskExceptionLite::SwitchSdmaCqeErrCodeToTsErrCode(u32 cqeErrCode) {
    switch (cqeErrCode) {
        case RT_SDMA_COMPERR:
            return TS_ERROR_SDMA_LINK_ERROR;
        case RT_SDMA_COMPDATAERR:
            return TS_ERROR_SDMA_POISON_ERROR;
        case RT_SDMA_DATAERR:
            return TS_ERROR_SDMA_DDRC_ERROR;
        default:
            return TS_ERROR_HCCL_OTHER_ERROR;
    }
}

HcclResult HcclCommTaskExceptionLite::PrintTaskContextInfo(u32 sqId, u32 taskId)
{
    auto queue = Hccl::GlobalMirrorTasks::Instance().GetQueue(devId_, sqId);
    CHK_PRT_RET(queue == nullptr,
        HCCL_ERROR("[%s]GetQueue nullptr, devId[%u], sqId[%u].", __func__, devId_, sqId), HCCL_E_PARA);

    auto func = [taskId] (const std::shared_ptr<Hccl::TaskInfo>& task) { return task->taskId_ == taskId; };
    auto taskItorPtr = queue->Find(func);
    CHK_PRT_RET(taskItorPtr == nullptr || *taskItorPtr == *queue->End(),
        HCCL_ERROR("[%s]exception task not found, devId[%u], sqId[%u], taskId[%u]", __func__, devId_, sqId, taskId),
        HCCL_E_PARA);

    // 找到当前异常task的前50个task(至多)
    std::vector<std::shared_ptr<Hccl::TaskInfo>> taskContext {};
    for (uint32_t i = 0; i < TASK_CONTEXT_SIZE && *taskItorPtr != *queue->Begin(); ++i, --(*taskItorPtr)) {
        if ((**taskItorPtr)->taskId_ > taskId) {
            HCCL_ERROR("[%s]prev taskId[%u] is bigger than err taskId[%u], stop traversal",
                __func__, (**taskItorPtr)->taskId_, taskId);
            break;
        }
        if ((**taskItorPtr)->taskId_ != taskId) {
            taskContext.emplace_back(**taskItorPtr);
        }
    }

    HCCL_ERROR("[TaskException][AICPU]context sequence before error task is "
        "[SDMA:M(rank), RDMA:RS(rank,id), SendPayload:SP(rank), InlineReduce:IR(rank), Reduce:R(rank), "
        "NotifyRecord:NR(rank,id), NotifyWait:NW(rank,id), SendNotify:SN(rank,id), "
        "WriteWithNotify:WN(rank,id), WriteReduceWithNotify:WRN(rank,id)]:");

    std::string taskContextInfo = "";
    Hccl::TaskInfo* lastTask = taskContext[0].get();
    for (u32 i = 0; i < taskContext.size(); ++i) {
        if (taskContext[i] == nullptr || taskContext[i]->dfxOpInfo_ == nullptr) {
            HCCL_ERROR("[%s]taskContext nullptr, taskContext[%u]=%p", __func__, i, taskContext[i]);
            continue;
        }
        std::string conciseInfo = taskContext[i]->GetConciseBaseInfo();
        conciseInfo += ",";

        if (taskContextInfo.size() + conciseInfo.size() >= TASK_CONTEXT_INFO_SIZE || // 1. 字符串超过一定长度时，打印一次
            lastTask->dfxOpInfo_->opIndex_ != taskContext[i]->dfxOpInfo_->opIndex_ ||    // 2. 不同算子，新起一行打印
            i + 1 == taskContext.size()) {                                           // 3. 遍历到最后一个task，打印一次
            HCCL_ERROR("[TaskException][AICPU]opData information is %s.", GetOpDataInfo(*lastTask).c_str());
            HCCL_ERROR("[TaskException][AICPU]task sequence is OP(%u): %s", lastTask->dfxOpInfo_->opIndex_, taskContextInfo.c_str());
            taskContextInfo = "";
            lastTask = taskContext[i].get();
        }
        taskContextInfo += conciseInfo;
    }
    HCCL_ERROR("[TaskException][AICPU]task sequence end.");
    return HCCL_SUCCESS;
}

std::string HcclCommTaskExceptionLite::GetGroupInfo(const Hccl::TaskInfo& taskInfo)
{
    if (taskInfo.dfxOpInfo_ == nullptr || taskInfo.dfxOpInfo_->comm_ == nullptr) {
        HCCL_ERROR("[%s]TaskInfo communicator is nullptr.", __func__);
        return "";
    }
    CollCommAicpu* aicpuComm = static_cast<CollCommAicpu*>(taskInfo.dfxOpInfo_->comm_);
    return Hccl::StringFormat("group:[%s], rankSize:[%u], localRank:[%d]",
 	    aicpuComm->GetIdentifier().c_str(), aicpuComm->GetTopoInfo().userRankSize, aicpuComm->GetTopoInfo().userRank);
}

std::string HcclCommTaskExceptionLite::GetOpDataInfo(const Hccl::TaskInfo& taskInfo)
{
    if (taskInfo.dfxOpInfo_ == nullptr) {
        HCCL_ERROR("[TaskInfo][%s]TaskInfo dfxOpInfo is nullptr.", __func__);
        return "";
    }

    const auto &opInfo = taskInfo.dfxOpInfo_;
    return Hccl::StringFormat("opIndex[%u], algTag[%s], count[%llu], reduceType[%s], src[0x%llx], dst[0x%llx], dataType[%s]",
        opInfo->opIndex_,
        opInfo->algTag_.c_str(),
        opInfo->op_.dataCount,
        opInfo->op_.reduceOp.Describe().c_str(),
        opInfo->op_.inputMem == nullptr ? 0 : static_cast<u64>(opInfo->op_.inputMem->GetAddr()),
        opInfo->op_.outputMem == nullptr ? 0 : static_cast<u64>(opInfo->op_.outputMem->GetAddr()),
        opInfo->op_.dataType.Describe().c_str());
}

void HcclCommTaskExceptionLite::PrintEid(const Hccl::TaskInfo& taskInfo)
{
    if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_REDUCE_INLINE ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_REDUCE_WITH_NOTIFY) {
        HCCL_ERROR("[TaskException][AICPU][%s]Error UB link info: localEid[%s], remoteEid[%s].", __func__,
            taskInfo.taskParam_.taskPara.Reduce.locEid.Describe().c_str(),
            taskInfo.taskParam_.taskPara.Reduce.rmtEid.Describe().c_str());
    } else if (taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB_INLINE_WRITE ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_WRITE_WITH_NOTIFY ||
        taskInfo.taskParam_.taskType == Hccl::TaskParamType::TASK_UB) {
        HCCL_ERROR("[TaskException][AICPU][%s]Error UB link info: localEid[%s], remoteEid[%s].", __func__,
            taskInfo.taskParam_.taskPara.DMA.locEid.Describe().c_str(),
            taskInfo.taskParam_.taskPara.DMA.rmtEid.Describe().c_str());
    }
}
}