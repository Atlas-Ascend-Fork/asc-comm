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

namespace ops_hccl_rs {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int INIT_SYNC_ID = 3;
constexpr int POST_SYNC_ID = 4;

static constexpr uint64_t SetBits(uint16_t end)
{
    return ((uint64_t(1) << (end + 1)) - uint64_t(1));
}

static uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum     = 8;
    constexpr uint16_t ctxIdShiftBit   = 45;
    constexpr uint16_t gsaBitNum       = 32;
    constexpr uint16_t gsaShiftBit     = 13;
    constexpr uint16_t loopNumBitNum   = 13;
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit) | ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit)
           | ((loopIterNum & SetBits(loopNumBitNum)) << loopNumShiftBit);
}

static uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum       = 7;
    constexpr uint16_t repeatNumShiftBit  = 55;
    constexpr uint16_t repeatLoopBitNum   = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum    = 7;
    constexpr uint16_t totalLoopShiftBit  = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit)
           | ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit)
           | ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

static uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset)
{
    constexpr uint16_t gsaBitNum   = 32;
    constexpr uint16_t gsaShiftBit = 21;
    constexpr uint16_t msBitNum    = 11;
    constexpr uint16_t msShiftBit  = 10;
    constexpr uint16_t ckeBitNum   = 10;
    constexpr uint16_t ckeShiftBit = 0;
    return ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit) | ((msOffset & SetBits(msBitNum)) << msShiftBit)
           | ((ckeOffset & SetBits(ckeBitNum)) << ckeShiftBit);
}

static void InitGroupCopyResources(ReduceScatterMesh1DMem2MemContext &ctx, ccu::LocalAddr *loopSrc,
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

static CcuResult GroupCopy(ReduceScatterMesh1DMem2MemContext &ctx, ccu::LocalAddr dst, ccu::LocalAddr src,
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
        ccu::LoopGroup group(paraCfg, offsetCfg, 1, grpLoops);
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
        ccu::LoopGroup group(goSize.parallelParam, offsetCfg, 2, grpLoops);
    }

    return CCU_SUCCESS;
}

static CcuResult ExecuteReduceScatterTransfer(ReduceScatterMesh1DMem2MemContext &ctx)
{
    ccu::LocalAddr localSrc;
    ccu::LocalAddr localDst;
    std::vector<ccu::RemoteAddr> remoteDst;

    remoteDst.resize(ctx.arg->rankSize);

    localSrc.addr = ctx.input;
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        localSrc.addr += ctx.rankSliceSize;
    }
    localSrc.token = ctx.inputToken;

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr = ctx.output[rankIdx];
            localDst.token = ctx.outputToken[rankIdx];
        } else {
            remoteDst[rankIdx].addr = ctx.output[rankIdx];
            remoteDst[rankIdx].token = ctx.outputToken[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        const uint16_t selfMask = 1 << ctx.arg->rankId;
        // 先用本 rank 输入中的第 rankId 段初始化本地输出。
        RETURN_IF_CCU_FAIL(GroupCopy(ctx, localDst, localSrc, ctx.goSize));
        RETURN_IF_CCU_FAIL(ccu::EventRecord(ctx.event, selfMask));
        RETURN_IF_CCU_FAIL(ccu::EventWait(ctx.event, selfMask));

        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            RETURN_IF_CCU_FAIL(ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << INIT_SYNC_ID));
        }
        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << INIT_SYNC_ID));
        }

        uint32_t channelId = 0;
        uint16_t remoteMask = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                // 第 rankIdx 段只归约到 rankIdx 的输出，而不是广播完整输入。
                ccu::LocalAddr remoteRankSrc;
                remoteRankSrc.addr = ctx.input;
                for (uint64_t i = 0; i < rankIdx; i++) {
                    remoteRankSrc.addr += ctx.rankSliceSize;
                }
                remoteRankSrc.token = ctx.inputToken;
                RETURN_IF_CCU_FAIL(ccu::WriteReduce(ctx.arg->channels[channelId], remoteDst[rankIdx],
                    remoteRankSrc, ctx.sliceSize,
                    HcclDataType::HCCL_DATA_TYPE_FP32, HcclReduceOp::HCCL_REDUCE_SUM, ctx.event, mask));
                remoteMask |= mask;
                channelId++;
            }
        }
        RETURN_IF_CCU_FAIL(ccu::EventWait(ctx.event, remoteMask));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuReduceScatterMesh1DMem2MemKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgReduceScatterMesh1DMem2Mem *>(arg);

    ReduceScatterMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    // 1. 初始化资源
    ctx.output.resize(ctx.arg->rankSize);
    ctx.outputToken.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.outputToken[peerId] = ccu::GetResByChannel<ccu::Variable>(
                ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    // 2. 加载参数
    uint32_t argId = 0;
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.input, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.output[ctx.arg->rankId], argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.inputToken, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.outputToken[ctx.arg->rankId], argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.rankSliceSize, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.sliceSize, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.addrOffset, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.loopParam, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.parallelParam, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.residual, argId++));

    // 3. 前置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.output[ctx.arg->rankId],
            OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i],
            ctx.outputToken[ctx.arg->rankId],
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
    }

    uint32_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBit));
    }

    // 4. 执行算法
    RETURN_IF_CCU_FAIL(ExecuteReduceScatterTransfer(ctx));

    // 5. 后置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID)); // 等待远端数据搬运完成
    }

    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_rs
