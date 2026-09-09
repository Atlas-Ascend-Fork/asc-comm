/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_kernel.h"

namespace ops_hccl_ag {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static void InitGroupCopyResources(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr* loopSrc, ccu::local_addr* loopDst, ccu::variable* loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.ms_interleave = ccu_ms_interleave;
        ctx.moConfig.loop_count = CCU_MS_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.mem_slice = CCU_LOCAL_COPY_MS_PER_LOOP * ccu_ms_size;

        ctx.moRes.eventCount = ctx.moConfig.loop_count;
        ctx.moRes.completed_event = ccu::array<ccu::event>(ctx.moRes.eventCount);

        ctx.moRes.bufCount = ctx.moConfig.loop_count * ctx.moConfig.ms_interleave;
        ctx.moRes.ccuBuf = ccu::array<ccu::ccu_buffer>(ctx.moRes.bufCount);

        ctx.resourceAllocated = true;
    }

    const std::string loopType = "localcopy";
    if (!ctx.IsLoopEntityRegistered(loopType)) {
        ctx.CreateLoopEntity(loopType);
        auto& entity = ctx.loopMap[loopType];
        for (uint32_t index = 0; index < 2; index++) {
            uint32_t bufBase = index * ctx.moConfig.ms_interleave;
            ccu::event loopEvt = ctx.moRes.completed_event[index];
            entity.body[index].reset(new ccu::func([&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                ccu::LocalCopy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
                ccu::LocalCopy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
            }));
            entity.loops[index].reset(new ccu::loop(entity.loop_param_[index], *entity.body[index]));
        }
    }
}

static CcuResult GroupCopy(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr dst_, ccu::local_addr src_, GroupOpSizeVars goSize)
{
    ccu::local_addr loopSrc[2];
    ccu::local_addr loopDst[2];
    ccu::variable loopLen[2];

    InitGroupCopyResources(ctx, loopSrc, loopDst, loopLen);

    auto& loops = ctx.loopMap["localcopy"];

    CCU_IF(goSize.addr_offset != 0)
    {
        ccu::variable loop_param_;
        loop_param_ = get_loop_param(0, ctx.moConfig.mem_slice * ctx.moConfig.loop_count, 0);
        loop_param_ += goSize.loop_param_;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;

        loopSrc[0].addr_ = src_.addr_;
        loopSrc[0].token = src_.token;
        loopDst[0].addr_ = dst_.addr_;
        loopDst[0].token = dst_.token;
        loopLen[0] = sliceSize;

        loops.loop_param_[0] = loop_param_;
        ccu::variable paraCfg;
        paraCfg = get_parallel_param(ctx.moConfig.loop_count - 1, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = get_offset_param(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);
        std::vector<ccu::loop> grpLoops{*loops.loops[0]};
        ccu::loop_group group(paraCfg, offsetCfg, ctx.moConfig.loop_count, grpLoops);
    }

    CCU_IF(goSize.parallel_param != 0)
    {
        src_.addr_ += goSize.addr_offset;
        dst_.addr_ += goSize.addr_offset;

        loopSrc[0].addr_ = src_.addr_;
        loopSrc[0].token = src_.token;
        loopDst[0].addr_ = dst_.addr_;
        loopDst[0].token = dst_.token;
        loopLen[0] = goSize.residual;

        src_.addr_ += goSize.residual;
        dst_.addr_ += goSize.residual;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;

        loopSrc[1].addr_ = src_.addr_;
        loopSrc[1].token = src_.token;
        loopDst[1].addr_ = dst_.addr_;
        loopDst[1].token = dst_.token;
        loopLen[1] = sliceSize;

        ccu::variable loopCfg0;
        loopCfg0 = get_loop_param(0, 0, 1);
        ccu::variable loopCfg1;
        loopCfg1 = get_loop_param(0, 0, 1);
        ccu::variable offsetCfg;
        offsetCfg = get_offset_param(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        loops.loop_param_[0] = loopCfg0;
        loops.loop_param_[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*loops.loops[0], *loops.loops[1]};
        ccu::loop_group group(goSize.parallel_param, offsetCfg, ctx.moConfig.loop_count, grpLoops);
    }

    return CCU_SUCCESS;
}

static CcuResult ExecuteAllGatherTransfer(AllGatherMesh1DMem2MemContext& ctx)
{
    ccu::local_addr src_;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> dst_;

    dst_.resize(ctx.arg->rankSize);

    src_.addr_ = ctx.input;
    src_.addr_ += ctx.currentRankSliceInputOffset;
    src_.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr_ = ctx.output[rankIdx];
            localDst.addr_ += ctx.currentRankSliceOutputOffset;
            localDst.token = ctx.token[rankIdx];
        } else {
            dst_[rankIdx].addr_ = ctx.output[rankIdx];
            dst_[rankIdx].addr_ += ctx.currentRankSliceOutputOffset;
            dst_[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        uint32_t channelId = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask_ = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                RETURN_IF_CCU_FAIL(
                    ccu::Write(ctx.arg->channels[channelId], dst_[rankIdx], src_, ctx.sliceSize, ctx.event, mask_));
                channelId++;
            }
        }
    }

    RETURN_IF_CCU_FAIL(GroupCopy(ctx, localDst, src_, ctx.goSize));
    RETURN_IF_CCU_FAIL(ccu::event_record(ctx.event, 1 << ctx.arg->rankId));

    const uint16_t totalMask = (1 << ctx.arg->rankSize) - 1;
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, totalMask));

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuAllGatherMesh1DMem2MemKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem*>(arg);

    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernel_arg;

    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    uint32_t arg_id_ = 0;
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.input, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.output[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.token[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.currentRankSliceInputOffset, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.currentRankSliceOutputOffset, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.sliceSize, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.addr_offset, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.loop_param_, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.parallel_param, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.residual, arg_id_++));

    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
    }

    uint32_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBit));
    }

    RETURN_IF_CCU_FAIL(ExecuteAllGatherTransfer(ctx));

    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }

    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_ag
