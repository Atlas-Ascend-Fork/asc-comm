/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_kernel.h"

namespace ops_hccl_bcast {

constexpr int BUFFER_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static CcuResult ExchangeBuffers(BroadcastMesh1DContext &ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.localBuffer,
            BUFFER_XN_ID, CKE_IDX_0, 1 << BUFFER_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.localToken,
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
    }
    const uint32_t allBits = (1 << BUFFER_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }
    return CCU_SUCCESS;
}

static CcuResult BroadcastFromRoot(BroadcastMesh1DContext &ctx)
{
    if (ctx.arg->rankId != ctx.arg->rootId) {
        return CCU_SUCCESS;
    }

    ccu::LocalAddr src;
    src.addr = ctx.localBuffer;
    src.token = ctx.localToken;
    uint32_t channelId = 0;
    uint16_t remoteMask = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            continue;
        }
        const uint16_t mask = 1 << rankIdx;
        ccu::RemoteAddr dst;
        dst.addr = ctx.remoteBuffer[rankIdx];
        dst.token = ctx.remoteToken[rankIdx];
        RETURN_IF_CCU_FAIL(ccu::Write(ctx.arg->channels[channelId], dst, src,
            ctx.dataSize, ctx.event, mask));
        remoteMask |= mask;
        channelId++;
    }
    RETURN_IF_CCU_FAIL(ccu::EventWait(ctx.event, remoteMask));
    return CCU_SUCCESS;
}

CcuResult CcuBroadcastMesh1DKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgBroadcastMesh1D *>(arg);
    BroadcastMesh1DContext ctx;
    ctx.arg = kernelArg;
    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    ctx.remoteBuffer.resize(ctx.arg->rankSize);
    ctx.remoteToken.resize(ctx.arg->rankSize);
    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.remoteBuffer[peerId] = ccu::GetResByChannel<ccu::Variable>(
                ctx.arg->channels[channelId], BUFFER_XN_ID);
            ctx.remoteToken[peerId] = ccu::GetResByChannel<ccu::Variable>(
                ctx.arg->channels[channelId], TOKEN_XN_ID);
            channelId++;
        }
    }

    uint32_t argId = 0;
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.localBuffer, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.localToken, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.dataSize, argId++));
    RETURN_IF_CCU_FAIL(ExchangeBuffers(ctx));
    RETURN_IF_CCU_FAIL(BroadcastFromRoot(ctx));

    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_bcast
