/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_groupcopy_demo.h"

static CcuResult AgInitResource(AllGatherContext& ctx)
{
    const auto* arg = ctx.arg;
    uint32_t channelIdx = 0;

    if (arg->channelCount == 0) {
        return CCU_E_PARA;
    }

    for (uint64_t peerId = 0; peerId < arg->rankSize; peerId++) {
        if (peerId != arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::variable>(arg->channels[channelIdx], AG_OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::variable>(arg->channels[channelIdx], AG_TOKEN_XN_ID);
            channelIdx++;
        }
    }

    ctx.resourceAllocated = false;
    ctx.groupCopyRegistered = false;
    return CCU_SUCCESS;
}

static CcuResult AgLoadArgs(AllGatherContext& ctx)
{
    const auto* arg = ctx.arg;
    uint32_t arg_id_ = 0;

    CCU_CHK_RET(ccu::load_arg(ctx.input, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.output[arg->rankId], arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.token[arg->rankId], arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.currentRankSliceInputOffset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.currentRankSliceOutputOffset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.tmpRepeatNum, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.inputRepeatStride, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.outputRepeatStride, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.normalSliceSize, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.lastSliceSize, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.isInputOutputEqual, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.addr_offset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.loop_param_, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.parallel_param, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.residual, arg_id_++));

    return CCU_SUCCESS;
}

static CcuResult AgPreSync(AllGatherContext& ctx)
{
    const auto* arg = ctx.arg;

    for (uint32_t i = 0; i < arg->channelCount; i++) {
        CCU_CHK_RET(ccu::write_variable_with_notify(
            arg->channels[i], ctx.output[arg->rankId], AG_OUTPUT_XN_ID, AG_CKE_IDX_0, 1 << AG_OUTPUT_XN_ID));
        CCU_CHK_RET(ccu::write_variable_with_notify(
            arg->channels[i], ctx.token[arg->rankId], AG_TOKEN_XN_ID, AG_CKE_IDX_0, 1 << AG_TOKEN_XN_ID));
    }

    const uint32_t allBit = (1 << AG_OUTPUT_XN_ID) | (1 << AG_TOKEN_XN_ID);
    for (uint32_t i = 0; i < arg->channelCount; i++) {
        CCU_CHK_RET(ccu::notify_wait(arg->channels[i], AG_CKE_IDX_0, allBit));
    }
    return CCU_SUCCESS;
}

static CcuResult AgPostSync(AllGatherContext& ctx)
{
    const auto* arg = ctx.arg;

    for (uint32_t i = 0; i < arg->channelCount; i++) {
        CCU_CHK_RET(ccu::notify_record(arg->channels[i], AG_CKE_IDX_0, 1 << AG_POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < arg->channelCount; i++) {
        CCU_CHK_RET(ccu::notify_wait(arg->channels[i], AG_CKE_IDX_0, 1 << AG_POST_SYNC_ID));
    }
    return CCU_SUCCESS;
}

static CcuResult AgDoAllGather(
    AllGatherContext& ctx, const ccu::local_addr& src_, std::vector<ccu::remote_addr>& dst_,
    const ccu::variable& sliceSize)
{
    const auto* arg = ctx.arg;
    uint32_t channelId = 0;

    for (uint64_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
        const uint16_t rankMask = 1 << rankIdx;
        if (rankIdx == arg->rankId) {
            CCU_CHK_RET(ccu::event_record(ctx.event, rankMask));
        } else {
            CCU_CHK_RET(ccu::Write(arg->channels[channelId], dst_[rankIdx], src_, sliceSize, ctx.event, rankMask));
            channelId++;
        }
    }

    CCU_IF(ctx.isInputOutputEqual == 0) { CCU_CHK_RET(AgGroupCopy(ctx, ctx.localDst, ctx.srcLocCopy, ctx.goSize)); }

    const uint16_t allRankMask = (1 << arg->rankSize) - 1;
    CCU_CHK_RET(ccu::event_wait(ctx.event, allRankMask));
    return CCU_SUCCESS;
}

static CcuResult AgDoRepeatAllGather(AllGatherContext& ctx)
{
    const auto* arg = ctx.arg;

    ccu::local_addr src_;
    std::vector<ccu::remote_addr> dst_;
    dst_.resize(arg->rankSize);

    src_.addr_ = ctx.input;
    src_.addr_ += ctx.currentRankSliceInputOffset;
    src_.token = ctx.token[arg->rankId];

    ctx.srcLocCopy.addr_ = ctx.input;
    ctx.srcLocCopy.addr_ += ctx.currentRankSliceInputOffset;
    ctx.srcLocCopy.token = ctx.token[arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
        if (rankIdx == arg->rankId) {
            ctx.localDst.addr_ = ctx.output[arg->rankId];
            ctx.localDst.addr_ += ctx.currentRankSliceOutputOffset;
            ctx.localDst.token = ctx.token[arg->rankId];
        } else {
            dst_[rankIdx].addr_ = ctx.output[rankIdx];
            dst_[rankIdx].addr_ += ctx.currentRankSliceOutputOffset;
            dst_[rankIdx].token = ctx.token[rankIdx];
        }
    }

    ccu::variable constVar1;
    ccu::variable repeatTimeflag;
    constVar1 = 1;
    repeatTimeflag = 0;

    CCU_WHILE(ctx.tmpRepeatNum != UINT64_MAX)
    {
        ctx.tmpRepeatNum += constVar1;

        CCU_IF(repeatTimeflag != 0)
        {
            src_.addr_ += ctx.inputRepeatStride;
            for (uint32_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
                if (rankIdx == arg->rankId) {
                    ctx.localDst.addr_ += ctx.outputRepeatStride;
                } else {
                    dst_[rankIdx].addr_ += ctx.outputRepeatStride;
                }
            }
        }

        CCU_IF(ctx.normalSliceSize != 0) { CCU_CHK_RET(AgDoAllGather(ctx, src_, dst_, ctx.normalSliceSize)); }

        repeatTimeflag = 1;
    }

    return CCU_SUCCESS;
}

CcuResult CcuAllGatherMesh1dMem2MemKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<AllGatherKernelArg*>(arg);

    AllGatherContext ctx;
    ctx.arg = kernel_arg;
    ctx.resourceAllocated = false;
    ctx.groupCopyRegistered = false;
    ctx.moConfig.ms_interleave = 0;
    ctx.moConfig.loop_count = 0;
    ctx.moConfig.mem_slice = 0;
    ctx.moRes.eventCount = 0;
    ctx.moRes.bufCount = 0;

    CCU_CHK_RET(AgInitResource(ctx));
    CCU_CHK_RET(AgLoadArgs(ctx));

    CCU_CHK_RET(AgPreSync(ctx));

    CCU_CHK_RET(AgDoRepeatAllGather(ctx));

    CCU_CHK_RET(AgPostSync(ctx));

    return CCU_SUCCESS;
}
