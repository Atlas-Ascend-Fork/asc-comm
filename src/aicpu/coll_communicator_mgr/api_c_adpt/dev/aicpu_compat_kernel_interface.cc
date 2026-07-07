/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>

#include "aicpu_indop_process.h"
#include "aicpu_operator_pub.h"
#include "channel_param.h"
#include "hccl_common.h"
#include "hcomm_primitives.h"
#include "log.h"

namespace {
struct AicpuCompatInitTask {
    u64 context;
    bool isCustom;
};

struct KfcResInitTask {
    u64 context;
    bool isCustom;
};

struct ThreadNotifyRecordParam {
    ThreadHandle thread;
    ThreadHandle dstThread;
    uint32_t dstNotifyIdx;
};

struct ThreadNotifyWaitParam {
    ThreadHandle thread;
    uint32_t notifyIdx;
};

HcclResult ValidateKfcClearPayload(const HcclKfcClearOpResTilingData *tilingData)
{
    CHK_PTR_NULL(tilingData);
    if (tilingData->magic != HCCL_KFC_CLEAR_OP_RES_MAGIC) {
        HCCL_ERROR("[RunAicpuKfcClearOpRes] magic mismatch, expect[0x%x], actual[0x%x]",
            HCCL_KFC_CLEAR_OP_RES_MAGIC, tilingData->magic);
        return HCCL_E_PARA;
    }
    if (tilingData->tagCount == 0 || tilingData->tagCount > HCCL_KFC_CLEAR_OP_RES_MAX_BATCH) {
        HCCL_ERROR("[RunAicpuKfcClearOpRes] invalid tagCount[%u], max[%u]",
            tilingData->tagCount, HCCL_KFC_CLEAR_OP_RES_MAX_BATCH);
        return HCCL_E_PARA;
    }
    return HCCL_SUCCESS;
}
}

extern "C" {
__attribute__((visibility("default"))) uint32_t RunAicpuIndOpCommInit(void *args)
{
    CHK_PTR_NULL(args);
    CommAicpuParam *commAicpuParam = reinterpret_cast<CommAicpuParam *>(args);
    HCCL_INFO("[RunAicpuIndOpCommInit] group[%s], deviceLogicId[%u], devicePhyId[%u], deviceType[%u]",
        commAicpuParam->hcomId, commAicpuParam->deviceLogicId, commAicpuParam->devicePhyId,
        commAicpuParam->deviceType);
    return AicpuIndopProcess::AicpuIndOpCommInit(commAicpuParam);
}

__attribute__((visibility("default"))) uint32_t RunAicpuIndOpChannelInit(void *args)
{
    CHK_PTR_NULL(args);
    AicpuCompatInitTask *ctxArgs = reinterpret_cast<AicpuCompatInitTask *>(args);
    HcclIndOpChannelRemoteResV3 *commParam =
        reinterpret_cast<HcclIndOpChannelRemoteResV3 *>(ctxArgs->context);
    CHK_PTR_NULL(commParam);
    HCCL_ERROR("[RunAicpuIndOpChannelInit] legacy V1 channel init is not supported in A5 AICPU data plane, "
        "group[%s], listNum[%u]", commParam->hcomId, commParam->listNum);
    return HCCL_E_PARA;
}

__attribute__((visibility("default"))) uint32_t RunAicpuKfcClearOpRes(void *args)
{
    CHK_PTR_NULL(args);
    KfcResInitTask *ctxArgs = reinterpret_cast<KfcResInitTask *>(args);
    if (ctxArgs->context == 0) {
        HCCL_ERROR("[RunAicpuKfcClearOpRes] context is null.");
        return HCCL_E_PARA;
    }

    const HcclKfcClearOpResTilingData *tilingData =
        reinterpret_cast<const HcclKfcClearOpResTilingData *>(ctxArgs->context);
    HcclResult ret = ValidateKfcClearPayload(tilingData);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[RunAicpuKfcClearOpRes] invalid payload, ret[%d]", ret), ret);

    HCCL_INFO("[RunAicpuKfcClearOpRes] group[%s], tagCount[%u]. A5 AICPU data plane has no per-tag resource "
        "clear path yet, skip as compatible success.", tilingData->group, tilingData->tagCount);
    return HCCL_SUCCESS;
}

__attribute__((visibility("default"))) uint32_t RunAicpuNotifyRecord(void *args)
{
    CHK_PTR_NULL(args);
    ThreadNotifyRecordParam *param = reinterpret_cast<ThreadNotifyRecordParam *>(args);
    HCCL_INFO("[RunAicpuNotifyRecord] thread[0x%llx], dstThread[0x%llx], dstNotifyIdx[%u]",
        param->thread, param->dstThread, param->dstNotifyIdx);
    int32_t ret = HcommThreadNotifyRecordOnThread(param->thread, param->dstThread, param->dstNotifyIdx);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[RunAicpuNotifyRecord] failed, ret[%d]", ret), ret);
    return HCCL_SUCCESS;
}

__attribute__((visibility("default"))) uint32_t RunAicpuNotifyWait(void *args)
{
    CHK_PTR_NULL(args);
    ThreadNotifyWaitParam *param = reinterpret_cast<ThreadNotifyWaitParam *>(args);
    HCCL_INFO("[RunAicpuNotifyWait] thread[0x%llx], notifyIdx[%u]", param->thread, param->notifyIdx);
    int32_t ret = HcommThreadNotifyWaitOnThreadWithDefaultTimeout(param->thread, param->notifyIdx);
    CHK_PRT_RET(ret != HCCL_SUCCESS, HCCL_ERROR("[RunAicpuNotifyWait] failed, ret[%d]", ret), ret);
    return HCCL_SUCCESS;
}
}
