/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 *
 * AllGather Mesh1D Mem2Mem CCU kernel. Data-plane logic ported from
 * asc-devkit/pr_4261 ccu_kernel_all_gather_mesh1d_mem2mem.cc; group-copy and
 * bit-packing helpers reused from asc-comm ccu_direct/01_allgather. Rewritten
 * against asc-comm's ccu:: free-function representation API.
 */
#include "ccu_kernel.h"

namespace ccu_ag_mem2mem {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

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
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit)
           | ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit)
           | ((loopIterNum & SetBits(loopNumBitNum)) << loopNumShiftBit);
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
    constexpr uint16_t ckeShiftBit = 0;
    return ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit)
           | ((msOffset & SetBits(msBitNum)) << msShiftBit)
           | ((ckeOffset & SetBits(ckeBitNum)) << ckeShiftBit);
}

static void InitGroupCopyResources(AllGatherMesh1DMem2MemContext &ctx, ccu::LocalAddr *loopSrc,
                                    ccu::LocalAddr *loopDst, ccu::Variable *loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.msInterleave = CCU_MS_INTERLEAVE;
        ctx.moConfig.loopCount = CCU_MS_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.memSlice = CCU_LOCAL_COPY_MS_PER_LOOP * CCU_MS_SIZE;

        ctx.moRes.eventCount = ctx.moConfig.loopCount;
        ctx.moRes.completedEvent = ccu::Array<ccu::Event>(ctx.moRes.eventCount);

        ctx.moRes.bufCount = ctx.moConfig.loopCount * ctx.moConfig.msInterleave;
        ctx.moRes.ccuBuf = ccu::Array<ccu::CcuBuffer>(ctx.moRes.bufCount);

        ctx.resourceAllocated = true;
    }

    std::string loopType = "localcopy";
    if (!ctx.IsLoopEntityRegistered(loopType)) {
        ctx.CreateLoopEntity(loopType);
        auto &entity = ctx.loopMap[loopType];
        for (uint32_t index = 0; index < 2; index++) {
            uint32_t bufBase = index * ctx.moConfig.msInterleave;
            ccu::Event loopEvt = ctx.moRes.completedEvent[index];
            entity.body[index].reset(new ccu::Func(
                [&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                    ccu::LocalCopy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                    ccu::EventWait(loopEvt, 1);
                    ccu::LocalCopy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                    ccu::EventWait(loopEvt, 1);
                }));
            entity.loops[index].reset(
                new ccu::Loop(entity.loopParam[index], *entity.body[index]));
        }
    }
}

static CcuResult GroupCopy(AllGatherMesh1DMem2MemContext &ctx, ccu::LocalAddr dst, ccu::LocalAddr src,
                            GroupOpSizeVars goSize)
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
        std::vector<ccu::Loop> grpLoops{ *loops.loops[0] };
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
        std::vector<ccu::Loop> grpLoops{ *loops.loops[0], *loops.loops[1] };
        ccu::LoopGroup group(goSize.parallelParam, offsetCfg, ctx.moConfig.loopCount, grpLoops);
    }

    return CCU_SUCCESS;
}

constexpr uint32_t BITS_PER_EVENT = 16;

static CcuResult InitResource(AllGatherMesh1DMem2MemContext &ctx)
{
    if (ctx.arg->channelCount == 0) {
        HCCL_ERROR("[CcuAllGatherMesh1DMem2Mem] channels is empty!");
        return CcuResult::CCU_E_INTERNAL;
    }
    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    // Remote slots come from the peer channels; local slots (output[rankId]/
    // token[rankId]/input) are filled by LoadArgs. Mirrors the KFC reference
    // kernel (asc-devkit pr_4261 ccu_kernel_kfc_all_gather_mesh1d_mem2mem).
    uint32_t channelIdx = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] =
                ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] =
                ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    // One event per 16 ranks; each rank records its own bit within its event.
    const uint32_t eventNum = (ctx.arg->rankSize + BITS_PER_EVENT - 1) / BITS_PER_EVENT;
    ctx.events.resize(eventNum);
    return CcuResult::CCU_SUCCESS;
}

static void LoadArgs(AllGatherMesh1DMem2MemContext &ctx)
{
    uint32_t argId = 0;
    ccu::LoadArg(ctx.input, argId++);
    ccu::LoadArg(ctx.output[ctx.arg->rankId], argId++);
    ccu::LoadArg(ctx.token[ctx.arg->rankId], argId++);
    ccu::LoadArg(ctx.currentRankSliceInputOffset, argId++);
    ccu::LoadArg(ctx.currentRankSliceOutputOffset, argId++);
    ccu::LoadArg(ctx.tmpRepeatNum, argId++);
    ccu::LoadArg(ctx.inputRepeatStride, argId++);
    ccu::LoadArg(ctx.outputRepeatStride, argId++);
    ccu::LoadArg(ctx.normalSliceSize, argId++);
    ccu::LoadArg(ctx.lastSliceSize, argId++);
    ccu::LoadArg(ctx.isInputOutputEqual, argId++);
    ccu::LoadArg(ctx.goSize.addrOffset, argId++);
    ccu::LoadArg(ctx.goSize.loopParam, argId++);
    ccu::LoadArg(ctx.goSize.parallelParam, argId++);
    ccu::LoadArg(ctx.goSize.residual, argId++);
}

static void PreSync(AllGatherMesh1DMem2MemContext &ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.output[ctx.arg->rankId],
            OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID);
        ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.token[ctx.arg->rankId],
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID);
    }
    uint16_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBit);
    }
}

static void PostSync(AllGatherMesh1DMem2MemContext &ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
}

// Aligned to the KFC reference kernel (asc-devkit pr_4261
// ccu_kernel_kfc_all_gather_mesh1d_mem2mem.cc): write/wait split with an event
// array grouped by rank (one event per 16 ranks, each rank records its own bit).
// No repeat loop, no isInputOutputEqual branch. Single mission, in != out.

// Phase 1: fire all remote writes + record self's event bit. No CCU_IF inside
// the per-rank loop (plain C++ if selects self vs peer).
static CcuResult DoAllGatherWrite(AllGatherMesh1DMem2MemContext &ctx,
    const ccu::LocalAddr &src, const std::vector<ccu::RemoteAddr> &dst)
{
    uint32_t channelIdx = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint32_t eventIdx = rankIdx / BITS_PER_EVENT;
        const uint16_t rankMask = 1U << (rankIdx % BITS_PER_EVENT);
        if (rankIdx == ctx.arg->rankId) {
            ccu::EventRecord(ctx.events[eventIdx], rankMask);
            continue;
        }
        ccu::Write(ctx.arg->channels[channelIdx], dst[rankIdx], src,
                   ctx.normalSliceSize, ctx.events[eventIdx], rankMask);
        channelIdx++;
    }
    return CCU_SUCCESS;
}

// Phase 2: wait every event's rank bits.
static CcuResult DoAllGatherWait(AllGatherMesh1DMem2MemContext &ctx)
{
    const uint32_t eventNum = static_cast<uint32_t>(ctx.events.size());
    for (uint32_t eventIdx = 0; eventIdx < eventNum; eventIdx++) {
        const uint32_t remainingRanks = ctx.arg->rankSize - eventIdx * BITS_PER_EVENT;
        const uint32_t bitCount = std::min<uint32_t>(remainingRanks, BITS_PER_EVENT);
        const uint16_t eventMask = bitCount == BITS_PER_EVENT
            ? static_cast<uint16_t>(UINT16_MAX)
            : static_cast<uint16_t>((1U << bitCount) - 1);
        ccu::EventWait(ctx.events[eventIdx], eventMask);
    }
    return CCU_SUCCESS;
}

static CcuResult DoAllGather(AllGatherMesh1DMem2MemContext &ctx)
{
    ccu::LocalAddr src;
    src.addr = ctx.input;
    src.addr += ctx.currentRankSliceInputOffset;
    src.token = ctx.token[ctx.arg->rankId];

    ccu::LocalAddr localDst;
    localDst.addr = ctx.output[ctx.arg->rankId];
    localDst.addr += ctx.currentRankSliceOutputOffset;
    localDst.token = ctx.token[ctx.arg->rankId];

    std::vector<ccu::RemoteAddr> dst(ctx.arg->rankSize);
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            continue;
        }
        dst[rankIdx].addr = ctx.output[rankIdx];
        dst[rankIdx].addr += ctx.currentRankSliceOutputOffset;
        dst[rankIdx].token = ctx.token[rankIdx];
    }

    // Fire all writes → local copy → wait all (KFC reference ordering).
    CCU_CHK_RET(DoAllGatherWrite(ctx, src, dst));
    GroupCopy(ctx, localDst, src, ctx.goSize);
    CCU_CHK_RET(DoAllGatherWait(ctx));
    return CCU_SUCCESS;
}

CcuResult CcuAllGatherMesh1DMem2MemKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem *>(arg);

    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    CCU_CHK_RET(InitResource(ctx));
    LoadArgs(ctx);
    PreSync(ctx);
    CCU_IF(ctx.normalSliceSize != 0)
    {
        DoAllGather(ctx);
    }
    PostSync(ctx);
    return CcuResult::CCU_SUCCESS;
}

}  // namespace ccu_ag_mem2mem
