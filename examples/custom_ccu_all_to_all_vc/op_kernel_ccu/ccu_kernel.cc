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

namespace ops_hccl_a2avc {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static CcuResult LoadArgs(AllToAllVCMesh1DContext& ctx)
{
    uint32_t arg_id_ = 0;
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.input, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.localOutput, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.inputToken, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.localOutputToken, arg_id_++));
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.sendBytes[rankIdx], arg_id_++));
        RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.sendOffsets[rankIdx], arg_id_++));
        RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.recvOffsets[rankIdx], arg_id_++));
    }
    return CCU_SUCCESS;
}

static CcuResult ExchangeOutput(AllToAllVCMesh1DContext& ctx)
{
    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId == ctx.arg->rankId) {
            continue;
        }
        ccu::variable peerDst;
        peerDst = ctx.localOutput;
        peerDst += ctx.recvOffsets[peerId];
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[channelId], peerDst, OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[channelId], ctx.localOutputToken, TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
        channelId++;
    }

    const uint32_t allBits = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }
    return CCU_SUCCESS;
}

static CcuResult ExecuteTransfer(AllToAllVCMesh1DContext& ctx)
{
    uint32_t channelId = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint16_t mask_ = 1 << rankIdx;
        ccu::local_addr src_;
        src_.addr_ = ctx.input;
        src_.addr_ += ctx.sendOffsets[rankIdx];
        src_.token = ctx.inputToken;

        CCU_IF(ctx.sendBytes[rankIdx] != 0)
        {
            if (rankIdx == ctx.arg->rankId) {
                ccu::local_addr dst_;
                dst_.addr_ = ctx.localOutput;
                dst_.addr_ += ctx.recvOffsets[rankIdx];
                dst_.token = ctx.localOutputToken;
                RETURN_IF_CCU_FAIL(ccu::LocalCopy(dst_, src_, ctx.sendBytes[rankIdx], ctx.event, mask_));
            } else {
                ccu::remote_addr dst_;
                dst_.addr_ = ctx.remoteOutput[rankIdx];
                dst_.token = ctx.remoteOutputToken[rankIdx];
                RETURN_IF_CCU_FAIL(
                    ccu::Write(ctx.arg->channels[channelId], dst_, src_, ctx.sendBytes[rankIdx], ctx.event, mask_));
            }
        }
        CCU_IF(ctx.sendBytes[rankIdx] == 0) { RETURN_IF_CCU_FAIL(ccu::event_record(ctx.event, mask_)); }
        if (rankIdx != ctx.arg->rankId) {
            channelId++;
        }
    }

    const uint16_t allRanksMask = (1 << ctx.arg->rankSize) - 1;
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, allRanksMask));
    return CCU_SUCCESS;
}

CcuResult CcuAllToAllVCMesh1DKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<CcuKernelArgAllToAllVCMesh1D*>(arg);
    AllToAllVCMesh1DContext ctx;
    ctx.arg = kernel_arg;
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
            ctx.remoteOutput[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelId], OUTPUT_XN_ID);
            ctx.remoteOutputToken[peerId] =
                ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelId], TOKEN_XN_ID);
            channelId++;
        }
    }

    RETURN_IF_CCU_FAIL(LoadArgs(ctx));
    RETURN_IF_CCU_FAIL(ExchangeOutput(ctx));
    RETURN_IF_CCU_FAIL(ExecuteTransfer(ctx));

    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_a2avc
