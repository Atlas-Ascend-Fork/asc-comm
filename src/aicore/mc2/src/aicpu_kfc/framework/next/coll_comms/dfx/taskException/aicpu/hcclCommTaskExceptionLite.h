/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef HCCL_COMM_TASKEXCEPTION_H
#define HCCL_COMM_TASKEXCEPTION_H

#include "daemon_func.h"
#include "mirror_task_manager.h"
#include "coll_comm_aicpu.h"
#include "aicpu_hccl_sqcq.h"
#include "error_message_v2.h"

namespace hcomm {

class HcclCommTaskExceptionLite : public Hccl::DaemonFunc {
public:
    static HcclCommTaskExceptionLite &GetInstance();
    void Init(u32 devId);
    void Call() override;

private:
    HcclCommTaskExceptionLite();
    ~HcclCommTaskExceptionLite();

    HcclResult ProcessCqe(CollCommAicpu *aicpuComm, const rtLogicCqReport_t &exceptionInfo);
    HcclResult HandleExceptionCqe();
    HcclResult GetThreadCqe(hccl::Thread* thread, rtLogicCqReport_t &cqeException, CqeStatus &cqeStatus);
    HcclResult GenerateErrorMessageReport(CollCommAicpu *aicpuComm, const Hccl::TaskInfo& taskInfo,
        const rtLogicCqReport_t &exceptionInfo, Hccl::ErrorMessageReport &errMsgInfo);
    void GetErrMsgInfo(const Hccl::TaskInfo& taskInfo, Hccl::ErrorMessageReport &errMsgInfo,
        const rtLogicCqReport_t &exceptionInfo);
    HcclResult SendTaskExceptionByMBox(const u32 notifyId, const u32 tsId, const rtLogicCqReport_t &exceptionInfo);
    uint16_t SwitchUBCqeErrCodeToTsErrCode(u32 cqeErrCode);
    uint16_t SwitchSdmaCqeErrCodeToTsErrCode(u32 cqeErrCode);
    HcclResult PrintTaskContextInfo(u32 sqId, u32 taskId);
    std::string GetGroupInfo(const Hccl::TaskInfo& taskInfo);
    std::string GetOpDataInfo(const Hccl::TaskInfo& taskInfo);

    void PrintEid(const Hccl::TaskInfo& taskInfo);
    std::string GetBaseInfo(const Hccl::TaskInfo& taskInfo);

    bool initFlag_{false};
    bool stopCall_{false};
    u32 devId_{INVALID_UINT};
    Hccl::MirrorTaskManager* mirrorTaskManager_{nullptr};  // 使用原始指针，不管理生命周期
};

}

#endif