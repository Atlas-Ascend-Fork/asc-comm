/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "log.h"
#include "exception_util.h"
#include "thread.h"
#include "aicpu_ts_thread.h"
#include "ub_transport_lite_impl.h"
#include "aicpu_ts_channel_helper.h"
#include "hcomm_pod_resource.h"
#include "hcomm_pod_resource_converter.h"

using namespace hccl;

namespace {

HcclResult ExportChannelResource(ChannelHandle legacyChannel, HcommChannelEntity *&podEntity)
{
    const auto *ctx = reinterpret_cast<const HcommAicpuChannelCtx *>(static_cast<uintptr_t>(legacyChannel));
    CHK_PTR_NULL(ctx);
    CHK_PRT_RET(ctx->abiHeader.version != HCOMM_AICPU_CHANNEL_CTX_VERSION ||
        ctx->abiHeader.magicWord != HCOMM_AICPU_CHANNEL_CTX_MAGIC_WORD || ctx->deviceChannel == nullptr,
        HCCL_ERROR("[%s] invalid legacy AICPU channel context.", __func__), HCCL_E_PARA);
    CHK_PRT_RET(ctx->engine != COMM_ENGINE_AICPU_TS,
        HCCL_ERROR("[%s] channel engine[%d] is not AICPU-TS.", __func__, ctx->engine), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(ctx->protocol != COMM_PROTOCOL_UBC_CTP && ctx->protocol != COMM_PROTOCOL_UBC_TP &&
        ctx->protocol != COMM_PROTOCOL_UB_MEM && ctx->protocol != COMM_PROTOCOL_UBOE &&
        ctx->protocol != COMM_PROTOCOL_UBG,
        HCCL_ERROR("[%s] channel protocol[%d] is not supported by the UB POD converter.", __func__, ctx->protocol),
        HCCL_E_NOT_SUPPORT);

    auto *transport = static_cast<Hccl::UbTransportLiteImpl *>(ctx->deviceChannel);
    CHK_PTR_NULL(transport);
    return transport->ExportPodResource(ctx->engine, ctx->protocol, podEntity);
}

} // namespace

int32_t HcommThreadConvertToPod(ThreadHandle legacyThread, ThreadHandle *podThread)
{
    CHK_PTR_NULL(podThread);
    CHK_PRT_RET(legacyThread == 0, HCCL_ERROR("[%s] legacy thread is 0.", __func__), HCCL_E_PTR);
    auto *thread = reinterpret_cast<Thread *>(static_cast<uintptr_t>(legacyThread));
    CHK_PTR_NULL(thread);
    auto *aicpuThread = dynamic_cast<AicpuTsThread *>(thread);
    CHK_PRT_RET(aicpuThread == nullptr || !aicpuThread->IsDeviceA5(),
        HCCL_ERROR("[%s] only A5 AICPU-TS threads can be converted to POD.", __func__), HCCL_E_NOT_SUPPORT);

    HcommThreadEntity *entity = nullptr;
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = aicpuThread->ExportPodResource(entity), ret = HCCL_E_INTERNAL);
    CHK_RET(ret);
    CHK_PTR_NULL(entity);
    *podThread = reinterpret_cast<ThreadHandle>(entity);
    return HCCL_SUCCESS;
}

int32_t HcommChannelConvertToPod(ChannelHandle legacyChannel, ChannelHandle *podChannel)
{
    CHK_PTR_NULL(podChannel);
    CHK_PRT_RET(legacyChannel == 0, HCCL_ERROR("[%s] legacy channel is 0.", __func__), HCCL_E_PTR);
    HcommChannelEntity *entity = nullptr;
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = ExportChannelResource(legacyChannel, entity), ret = HCCL_E_INTERNAL);
    CHK_RET(ret);
    CHK_PTR_NULL(entity);
    *podChannel = reinterpret_cast<ChannelHandle>(entity);
    return HCCL_SUCCESS;
}
