/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "coll_comm_aicpu_destroy_func.h"
#include "aicpu_indop_process.h"
#include "read_write_lock.h"

namespace hccl {
CollCommAicpuDestroyFunc &CollCommAicpuDestroyFunc::GetInstance()
{
    static CollCommAicpuDestroyFunc func;
    return func;
}

void CollCommAicpuDestroyFunc::Call()
{
    if (stopCall_ == true) {
        return;
    }

    HcclResult ret = Process();
    if (ret != HCCL_SUCCESS) {
        stopCall_ = true;
        HCCL_ERROR("[%s]Process fail, set stopCall_[%d] ret[%d]", __func__, stopCall_, ret);
    }
}

HcclResult CollCommAicpuDestroyFunc::Process()
{
    ReadWriteLockBase &commAicpuMapMutex = AicpuIndopProcess::AicpuGetCommMutex();
    ReadWriteLock rwlock(commAicpuMapMutex);
    rwlock.readLock();

    std::vector<std::pair<std::string, CollCommAicpuMgr *>> aicpuCommInfo;
    CHK_RET(AicpuIndopProcess::AicpuGetCommAll(aicpuCommInfo));
    std::vector<std::string> destroyComm;

    for (auto &commInfo : aicpuCommInfo) {
        CollCommAicpu *aicpuComm = commInfo.second->GetCollCommAicpu();
        CHK_PTR_NULL(aicpuComm);

        if (aicpuComm->GetCommmStatus() != HcclCommStatus::HCCL_COMM_STATUS_READY) {
            continue;
        }

        Hccl::KfcCommand cmd = Hccl::KfcCommand::NONE;
        CHK_RET(aicpuComm->BackGroundGetCmd(cmd));
        if (cmd != Hccl::KfcCommand::DESTROY_AICPU_COMM) {
            continue;
        }
        destroyComm.push_back(aicpuComm->GetIdentifier());
        CHK_RET(aicpuComm->BackGroundSetStatus(Hccl::KfcStatus::DESTROY_AICPU_COMM_DONE));
        HCCL_RUN_INFO("[%s]group[%s] Recv DESTROY_AICPU_COMM cmd and set DESTROY_AICPU_COMM_DONE",
            __func__, aicpuComm->GetIdentifier().c_str());
    }
    rwlock.readUnlock();

    rwlock.writeLock();
    for (std::string& groupName : destroyComm) {
        CHK_RET(AicpuIndopProcess::AicpuDestroyCommbyGroup(groupName));
    }
    rwlock.writeUnlock();
    return HCCL_SUCCESS;
}
}  // namespace hccl
