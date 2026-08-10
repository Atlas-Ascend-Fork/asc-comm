/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
#include "acl/acl_rt.h"
#include "adv_api/hcomm/hcomm.h"
#include "sample_allgather_def.h"

using namespace AscendC;

#define EXPORT_AIV_META_INFO(kernel_name) \
static const struct FunLevelKType kernel_name##_kernel_type_section __attribute__ \
((used, section (".ascend.meta." #kernel_name))) \
= {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}}

extern "C" __vector__ __global__ __aicore__ void SampleAllGatherCustomKernel(__gm__ SampleAllGatherContext *ctx)
{
    if (ctx == nullptr) {
        return;
    }

    Hcomm<COMM_PROTOCOL_UBC_CTP> hcomm;
    __ubuf__ uint8_t workspace[HCOMM_DFX_WORKSPACE_SIZE];
    (void)hcomm.Init(workspace, HCOMM_DFX_WORKSPACE_SIZE);

    ChannelHandle channel = static_cast<ChannelHandle>(ctx->channelHandle);
    GM_ADDR src = reinterpret_cast<GM_ADDR>(ctx->localBufferAddr);
    GM_ADDR dst = reinterpret_cast<GM_ADDR>(ctx->remoteBufferAddr);
    uint64_t len = ctx->len == 0 ? SAMPLE_ALLGATHER_NORMAL_LEN : ctx->len;

    ctx->writeRet = hcomm.WriteNbi(channel, dst, src, len);
    ctx->drainRet = hcomm.Drain(channel);
}

EXPORT_AIV_META_INFO(SampleAllGatherCustomKernel);

extern "C" __vector__ __global__ __aicore__ void SampleAllGatherCallbackFaultKernel(
    __gm__ SampleAllGatherContext *ctx)
{
    (void)ctx;
    trap();
}

EXPORT_AIV_META_INFO(SampleAllGatherCallbackFaultKernel);

void run_sample_allgather_custom(SampleAllGatherContext *ctx, aclrtStream stream)
{
    SampleAllGatherCustomKernel<<<1, 0, stream>>>(ctx);
}

void run_sample_allgather_callback_fault(SampleAllGatherContext *ctx, aclrtStream stream)
{
    SampleAllGatherCallbackFaultKernel<<<1, 0, stream>>>(ctx);
}
