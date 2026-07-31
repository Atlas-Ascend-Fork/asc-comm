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

namespace CcuAgNhr1dMem2mem {

constexpr int OUTPUT_XN_ID = 0;
constexpr int TOKEN_XN_ID = 1;
constexpr int POST_SYNC_ID = 2;
constexpr int STEP_POST_SYNC_ID = 4;
constexpr int CKE_IDX_0 = 0;
constexpr uint16_t TRANSFER_EVENT_MASK = 1;

static constexpr uint64_t SetBits(uint16_t end)
{
    return ((uint64_t(1) << (end + 1)) - uint64_t(1));
}

static uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum = 8;
    constexpr uint16_t ctxIdShiftBit = 45;
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 13;
    constexpr uint16_t loopNumBitNum = 13;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit)
           | ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit)
           | (loopIterNum & SetBits(loopNumBitNum));
}

static uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit)
           | ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit)
           | ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

static uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset)
{
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 21;
    constexpr uint16_t msBitNum = 11;
    constexpr uint16_t msShiftBit = 10;
    constexpr uint16_t ckeBitNum = 10;
    return ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit)
           | ((msOffset & SetBits(msBitNum)) << msShiftBit)
           | (ckeOffset & SetBits(ckeBitNum));
}

static void InitGroupCopyResources(AllGatherNhr1DMem2MemContext &ctx, ccu::LocalAddr *loopSrc,
    ccu::LocalAddr *loopDst, ccu::Variable *loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.msInterleave = CCU_MS_INTERLEAVE;
        ctx.moConfig.loopCount = CCU_MS_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.memSlice = CCU_LOCAL_COPY_MS_PER_LOOP * CCU_MS_SIZE;

        ctx.moRes.completedEvent = ccu::Array<ccu::Event>(ctx.moConfig.loopCount);
        ctx.moRes.ccuBuf = ccu::Array<ccu::CcuBuffer>(
            ctx.moConfig.loopCount * ctx.moConfig.msInterleave);
        ctx.resourceAllocated = true;
    }

    const std::string loopType = "localcopy";
    if (!ctx.IsLoopEntityRegistered(loopType)) {
        ctx.CreateLoopEntity(loopType);
        auto &entity = ctx.loopMap[loopType];
        for (uint32_t index = 0; index < 2; index++) {
            const uint32_t bufBase = index * ctx.moConfig.msInterleave;
            ccu::Event loopEvt = ctx.moRes.completedEvent[index];
            entity.body[index].reset(new ccu::Func(
                [&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                    ccu::LocalCopy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                    ccu::EventWait(loopEvt, 1);
                    ccu::LocalCopy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                    ccu::EventWait(loopEvt, 1);
                }));
            entity.loops[index].reset(new ccu::Loop(entity.loopParam[index], *entity.body[index]));
        }
    }
}

static CcuResult GroupCopy(AllGatherNhr1DMem2MemContext &ctx, ccu::LocalAddr dst,
    ccu::LocalAddr src, GroupOpSizeVars goSize)
{
    ccu::LocalAddr loopSrc[2];
    ccu::LocalAddr loopDst[2];
    ccu::Variable loopLen[2];

    InitGroupCopyResources(ctx, loopSrc, loopDst, loopLen);
    auto &loops = ctx.loopMap["localcopy"];

    CCU_IF(goSize.addrOffset != 0)
    {
        ccu::Variable loopParam;
        loopParam = GetLoopParam(0, ctx.moConfig.memSlice * ctx.moConfig.loopCount, 0);
        loopParam += goSize.loopParam;

        ccu::Variable sliceSize;
        sliceSize = ctx.moConfig.memSlice;
        loopSrc[0].addr = src.addr;
        loopSrc[0].token = src.token;
        loopDst[0].addr = dst.addr;
        loopDst[0].token = dst.token;
        loopLen[0] = sliceSize;

        loops.loopParam[0] = loopParam;
        ccu::Variable paraCfg;
        paraCfg = GetParallelParam(ctx.moConfig.loopCount - 1, 0, 1);
        ccu::Variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.memSlice, ctx.moConfig.msInterleave, 1);
        std::vector<ccu::Loop> grpLoops{*loops.loops[0]};
        ccu::LoopGroup group(paraCfg, offsetCfg, ctx.moConfig.loopCount, grpLoops);
    }

    CCU_IF(goSize.parallelParam != 0)
    {
        src.addr += goSize.addrOffset;
        dst.addr += goSize.addrOffset;

        loopSrc[0].addr = src.addr;
        loopSrc[0].token = src.token;
        loopDst[0].addr = dst.addr;
        loopDst[0].token = dst.token;
        loopLen[0] = goSize.residual;

        src.addr += goSize.residual;
        dst.addr += goSize.residual;

        ccu::Variable sliceSize;
        sliceSize = ctx.moConfig.memSlice;
        loopSrc[1].addr = src.addr;
        loopSrc[1].token = src.token;
        loopDst[1].addr = dst.addr;
        loopDst[1].token = dst.token;
        loopLen[1] = sliceSize;

        ccu::Variable loopCfg0;
        loopCfg0 = GetLoopParam(0, 0, 1);
        ccu::Variable loopCfg1;
        loopCfg1 = GetLoopParam(0, 0, 1);
        ccu::Variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.memSlice, ctx.moConfig.msInterleave, 1);

        loops.loopParam[0] = loopCfg0;
        loops.loopParam[1] = loopCfg1;
        std::vector<ccu::Loop> grpLoops{*loops.loops[0], *loops.loops[1]};
        ccu::LoopGroup group(goSize.parallelParam, offsetCfg, ctx.moConfig.loopCount, grpLoops);
    }
    return CcuResult::CCU_SUCCESS;
}

static CcuResult InitResource(AllGatherNhr1DMem2MemContext &ctx)
{
    if (ctx.arg->rankSize < 2 || ctx.arg->rankId >= ctx.arg->rankSize ||
        ctx.arg->channelCount != ctx.arg->rank2ChannelIdx.size()) {
        return CcuResult::CCU_E_PARA;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);
    for (const auto &rankAndChannel : ctx.arg->rank2ChannelIdx) {
        const uint32_t peerRank = rankAndChannel.first;
        const uint32_t channelIdx = rankAndChannel.second;
        if (peerRank >= ctx.arg->rankSize || channelIdx >= ctx.arg->channelCount) {
            return CcuResult::CCU_E_PARA;
        }
        ctx.output[peerRank] =
            ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
        ctx.token[peerRank] =
            ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
    }
    return CcuResult::CCU_SUCCESS;
}

static void LoadArgs(AllGatherNhr1DMem2MemContext &ctx)
{
    ccu::LoadArg(ctx.input, TASK_INPUT_ADDR);
    ccu::LoadArg(ctx.output[ctx.arg->rankId], TASK_OUTPUT_ADDR);
    ccu::LoadArg(ctx.token[ctx.arg->rankId], TASK_TOKEN);
    ccu::LoadArg(ctx.inputSliceStride, TASK_INPUT_SLICE_STRIDE);
    ccu::LoadArg(ctx.outputSliceStride, TASK_OUTPUT_SLICE_STRIDE);
    ccu::LoadArg(ctx.chunkSize, TASK_CHUNK_SIZE);
    ccu::LoadArg(ctx.goSize.addrOffset, TASK_GO_ADDR_OFFSET);
    ccu::LoadArg(ctx.goSize.loopParam, TASK_GO_LOOP_PARAM);
    ccu::LoadArg(ctx.goSize.parallelParam, TASK_GO_PARALLEL_PARAM);
    ccu::LoadArg(ctx.goSize.residual, TASK_GO_RESIDUAL);
}

static void PreSync(AllGatherNhr1DMem2MemContext &ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.output[ctx.arg->rankId],
            OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID);
        ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.token[ctx.arg->rankId],
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID);
    }
    const uint16_t allBits = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBits);
    }
}

static void PostSync(AllGatherNhr1DMem2MemContext &ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
}

static CcuResult CopyLocalSlice(AllGatherNhr1DMem2MemContext &ctx)
{
    ccu::LocalAddr src;
    src.addr = ctx.input;
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        src.addr += ctx.inputSliceStride;
    }
    src.token = ctx.token[ctx.arg->rankId];

    ccu::LocalAddr dst;
    dst.addr = ctx.output[ctx.arg->rankId];
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        dst.addr += ctx.outputSliceStride;
    }
    dst.token = ctx.token[ctx.arg->rankId];
    return GroupCopy(ctx, dst, src, ctx.goSize);
}

static CcuResult RunNhrStep(AllGatherNhr1DMem2MemContext &ctx,
    const NhrStepInfo &stepInfo, bool isLastStep)
{
    const auto sendIt = ctx.arg->rank2ChannelIdx.find(stepInfo.toRank);
    const auto recvIt = ctx.arg->rank2ChannelIdx.find(stepInfo.fromRank);
    if (sendIt == ctx.arg->rank2ChannelIdx.end() ||
        recvIt == ctx.arg->rank2ChannelIdx.end()) {
        return CcuResult::CCU_E_NOT_FOUND;
    }

    const uint32_t sendChannelIdx = sendIt->second;
    const uint32_t recvChannelIdx = recvIt->second;
    const ChannelHandle sendChannel = ctx.arg->channels[sendChannelIdx];
    const ChannelHandle recvChannel = ctx.arg->channels[recvChannelIdx];

    for (uint32_t sliceIdx : stepInfo.txSliceIdxs) {
        ccu::LocalAddr src;
        if (stepInfo.step == 0) {
            src.addr = ctx.input;
            for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
                src.addr += ctx.inputSliceStride;
            }
        } else {
            src.addr = ctx.output[ctx.arg->rankId];
            for (uint32_t i = 0; i < sliceIdx; i++) {
                src.addr += ctx.outputSliceStride;
            }
        }
        src.token = ctx.token[ctx.arg->rankId];

        ccu::RemoteAddr dst;
        dst.addr = ctx.output[stepInfo.toRank];
        for (uint32_t i = 0; i < sliceIdx; i++) {
            dst.addr += ctx.outputSliceStride;
        }
        dst.token = ctx.token[stepInfo.toRank];

        ccu::Write(sendChannel, dst, src, ctx.chunkSize, ctx.transferEvent,
            TRANSFER_EVENT_MASK);
        ccu::EventWait(ctx.transferEvent, TRANSFER_EVENT_MASK);
    }

    // The next step may forward slices received in this step. Do not enter it
    // until the incoming writer has completed.
    if (!isLastStep) {
        ccu::NotifyRecord(sendChannel, CKE_IDX_0, 1 << STEP_POST_SYNC_ID);
        ccu::NotifyWait(recvChannel, CKE_IDX_0, 1 << STEP_POST_SYNC_ID);
    }
    return CcuResult::CCU_SUCCESS;
}

static CcuResult DoAllGather(AllGatherNhr1DMem2MemContext &ctx)
{
    RETURN_IF_CCU_FAIL(CopyLocalSlice(ctx));
    for (uint32_t stepIdx = 0; stepIdx < ctx.arg->stepInfoVector.size(); stepIdx++) {
        const bool isLastStep = (stepIdx + 1 == ctx.arg->stepInfoVector.size());
        RETURN_IF_CCU_FAIL(RunNhrStep(
            ctx, ctx.arg->stepInfoVector[stepIdx], isLastStep));
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuAllGatherNhr1DMem2MemKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgAllGatherNhr1DMem2Mem *>(arg);
    AllGatherNhr1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    RETURN_IF_CCU_FAIL(InitResource(ctx));
    LoadArgs(ctx);
    PreSync(ctx);
    RETURN_IF_CCU_FAIL(DoAllGather(ctx));
    PostSync(ctx);
    return CcuResult::CCU_SUCCESS;
}

}  // namespace CcuAgNhr1dMem2mem
