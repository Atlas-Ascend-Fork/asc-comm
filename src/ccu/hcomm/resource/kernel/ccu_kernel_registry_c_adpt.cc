/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_res.h"

#include "adapter_rts_common.h"

#include "hcomm/resource/kernel/ccu_kernel_registry_mgr.h"
#include "hcomm/common/ccu_log.h"
#include "hcom_common.h"

// 以下三个接口由 hcomm 的 MyRank 在建立/拆除通信域时调用，为该通信域分配与回收
// kernel 注册上下文。它们只操作 asc 侧的数据面状态，不拉起驱动、不申请硬件资源。
//
// deviceLogicId 必须显式传入：CcuInsHandle 由 per-device 计数器分配且各自从 0 起，
// 跨设备会重号，仅凭 handle 无法定位上下文，也不得依赖线程当前设备推断。
CcuResult HcommCcuKernelRegistryCreate(int32_t deviceLogicId, CcuInsHandle* handle)
{
    CCU_CHK_PTR_NULL(handle);
    CCU_CHK_RET(hcomm::CcuKernelRegistryMgr::GetInstance(deviceLogicId).Create(deviceLogicId, *handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuKernelRegistryDestroy(CcuInsHandle handle, int32_t deviceLogicId)
{
    // ~CcuKernelRegistry 会注销 kernel 并归还指令区间，这些操作要求目标设备为当前设备，
    // 而销毁线程可能位于其他设备上，因此需要切换并在返回前恢复。
    const int32_t threadDevId = HcclGetThreadDeviceId();
    const bool isDiffDevId = (deviceLogicId != threadDevId);
    if (isDiffDevId) {
        HCCL_INFO("[%s] deviceLogicId[%d], threadDevId[%d]", __func__, deviceLogicId, threadDevId);
        CCU_CHK_RET(hrtSetDevice(deviceLogicId));
    }

    CcuResult ret = hcomm::CcuKernelRegistryMgr::GetInstance(deviceLogicId).Destroy(handle);
    if (ret != CcuResult::CCU_SUCCESS) {
        HCCL_ERROR("[%s] destroy kernel registry failed, ret[%d].", __func__, ret);
    }

    // 无论销毁结果如何都要恢复原设备，避免污染调用方的设备上下文。
    if (isDiffDevId) {
        CCU_CHK_RET(hrtSetDevice(threadDevId));
    }
    return ret;
}

CcuResult HcommCcuKernelRegistryExists(CcuInsHandle handle, int32_t deviceLogicId, uint32_t* exists)
{
    CCU_CHK_PTR_NULL(exists);
    // 句柄不存在属于正常查询结果，用 Exists 而非 Get，避免产生无谓的错误日志。
    *exists = hcomm::CcuKernelRegistryMgr::GetInstance(deviceLogicId).Exists(handle) ? 1U : 0U;
    return CcuResult::CCU_SUCCESS;
}
