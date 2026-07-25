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

namespace ops_hccl_a2av {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static CcuResult LoadArgs(AllToAllVMesh1DContext &ctx)
{
    uint32_t argId = 0;
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.input, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.localOutput, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.inputToken, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.localOutputToken, argId++));
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.sendBytes[rankIdx], argId++));
        RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.sendOffsets[rankIdx], argId++));
        RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.recvOffsets[rankIdx], argId++));
    }
    return CCU_SUCCESS;
}

static CcuResult ExchangeOutput(AllToAllVMesh1DContext &ctx)
{
    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId == ctx.arg->rankId) {
            continue;
        }
        ccu::Variable peerDst;
        peerDst = ctx.localOutput;
        peerDst += ctx.recvOffsets[peerId];
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[channelId], peerDst,
            OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[channelId], ctx.localOutputToken,
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
        channelId++;
    }

    const uint32_t allBits = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }
    return CCU_SUCCESS;
}

static CcuResult ExecuteTransfer(AllToAllVMesh1DContext &ctx)
{
    uint32_t channelId = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint16_t mask = 1 << rankIdx;
        ccu::LocalAddr src;
        src.addr = ctx.input;
        src.addr += ctx.sendOffsets[rankIdx];
        src.token = ctx.inputToken;

        CCU_IF(ctx.sendBytes[rankIdx] != 0)
        {
            if (rankIdx == ctx.arg->rankId) {
                ccu::LocalAddr dst;
                dst.addr = ctx.localOutput;
                dst.addr += ctx.recvOffsets[rankIdx];
                dst.token = ctx.localOutputToken;
                RETURN_IF_CCU_FAIL(ccu::LocalCopy(dst, src, ctx.sendBytes[rankIdx], ctx.event, mask));
            } else {
                ccu::RemoteAddr dst;
                dst.addr = ctx.remoteOutput[rankIdx];
                dst.token = ctx.remoteOutputToken[rankIdx];
                RETURN_IF_CCU_FAIL(ccu::Write(ctx.arg->channels[channelId], dst, src,
                    ctx.sendBytes[rankIdx], ctx.event, mask));
            }
        }
        CCU_IF(ctx.sendBytes[rankIdx] == 0)
        {
            RETURN_IF_CCU_FAIL(ccu::EventRecord(ctx.event, mask));
        }
        if (rankIdx != ctx.arg->rankId) {
            channelId++;
        }
    }

    const uint16_t allRanksMask = (1 << ctx.arg->rankSize) - 1;
    RETURN_IF_CCU_FAIL(ccu::EventWait(ctx.event, allRanksMask));
    return CCU_SUCCESS;
}

CcuResult CcuAllToAllVMesh1DKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgAllToAllVMesh1D *>(arg);
    AllToAllVMesh1DContext ctx;
    ctx.arg = kernelArg;
    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    ctx.remoteOutput.resize(ctx.arg->rankSize);
    ctx.remoteOutputToken.resize(ctx.arg->rankSize);
    ctx.sendBytes.resize(ctx.arg->rankSize);
    ctx.sendOffsets.resize(ctx.arg->rankSize);
    ctx.recvOffsets.resize(ctx.arg->rankSize);

    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.remoteOutput[peerId] = ccu::GetResByChannel<ccu::Variable>(
                ctx.arg->channels[channelId], OUTPUT_XN_ID);
            ctx.remoteOutputToken[peerId] = ccu::GetResByChannel<ccu::Variable>(
                ctx.arg->channels[channelId], TOKEN_XN_ID);
            channelId++;
        }
    }

    RETURN_IF_CCU_FAIL(LoadArgs(ctx));
    RETURN_IF_CCU_FAIL(ExchangeOutput(ctx));
    RETURN_IF_CCU_FAIL(ExecuteTransfer(ctx));

    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_a2av
