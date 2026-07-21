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

namespace ops_hccl_ar {

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

static CcuResult CreateGroupCopy(AllReduceMesh1DMem2MemContext &ctx)
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

    if (ctx.groupCopyRegistered) {
        return CCU_SUCCESS;
    }

    for (uint32_t index = 0; index < 2; index++) {
        ccu::Event loopEvt = ctx.moRes.completedEvent[index];
        ccu::CcuBuffer &buf = ctx.moRes.ccuBuf[index * ctx.moConfig.msInterleave];
        ctx.copyBody[index].reset(new ccu::Func(
            [&ctx, index, buf, loopEvt]() {
                ccu::LocalCopy(buf, ctx.loopSrc[index], ctx.loopLen[index], loopEvt, 1);
                ccu::EventWait(loopEvt, 1);
                ccu::LocalCopy(ctx.loopDst[index], buf, ctx.loopLen[index], loopEvt, 1);
                ccu::EventWait(loopEvt, 1);
            }));
        ctx.copyLoops[index].reset(new ccu::Loop(ctx.copyLoopParam[index], *ctx.copyBody[index]));
    }
    ctx.groupCopyRegistered = true;
    return CCU_SUCCESS;
}

static CcuResult GroupCopy(AllReduceMesh1DMem2MemContext &ctx, ccu::LocalAddr dst, ccu::LocalAddr src,
                           GroupOpSizeVars &goSize)
{
    RETURN_IF_CCU_FAIL(CreateGroupCopy(ctx));

    CCU_IF(goSize.addrOffset != 0)
    {
        ccu::Variable loopParam;
        loopParam = GetLoopParam(0, ctx.moConfig.memSlice * ctx.moConfig.loopCount, 0);
        loopParam += goSize.loopParam;
        ctx.loopSrc[0] = src;
        ctx.loopDst[0] = dst;
        ctx.loopLen[0] = ctx.moConfig.memSlice;
        ctx.copyLoopParam[0] = loopParam;

        ccu::Variable parallelCfg;
        parallelCfg = GetParallelParam(ctx.moConfig.loopCount - 1, 0, 1);
        ccu::Variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.memSlice, ctx.moConfig.msInterleave, 1);
        std::vector<ccu::Loop> loops{*ctx.copyLoops[0]};
        ccu::LoopGroup group(parallelCfg, offsetCfg, 1, loops);
    }

    CCU_IF(goSize.parallelParam != 0)
    {
        src.addr += goSize.addrOffset;
        dst.addr += goSize.addrOffset;
        ctx.loopSrc[0] = src;
        ctx.loopDst[0] = dst;
        ctx.loopLen[0] = goSize.residual;

        src.addr += goSize.residual;
        dst.addr += goSize.residual;
        ctx.loopSrc[1] = src;
        ctx.loopDst[1] = dst;
        ctx.loopLen[1] = ctx.moConfig.memSlice;

        ccu::Variable loopCfg0;
        loopCfg0 = GetLoopParam(0, 0, 1);
        ccu::Variable loopCfg1;
        loopCfg1 = GetLoopParam(0, 0, 1);
        ctx.copyLoopParam[0] = loopCfg0;
        ctx.copyLoopParam[1] = loopCfg1;
        ccu::Variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.memSlice, ctx.moConfig.msInterleave, 1);
        std::vector<ccu::Loop> loops{*ctx.copyLoops[0], *ctx.copyLoops[1]};
        ccu::LoopGroup group(goSize.parallelParam, offsetCfg, 2, loops);
    }
    return CCU_SUCCESS;
}

static CcuResult ExecuteAllReduceTransfer(AllReduceMesh1DMem2MemContext &ctx)
{
    ccu::LocalAddr src;
    ccu::LocalAddr localDst;
    std::vector<ccu::RemoteAddr> dst;

    dst.resize(ctx.arg->rankSize);

    src.addr = ctx.input;
    src.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr = ctx.output[rankIdx];
            localDst.token = ctx.token[rankIdx];
        } else {
            dst[rankIdx].addr = ctx.output[rankIdx];
            dst[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        const uint16_t selfMask = 1 << ctx.arg->rankId;
        RETURN_IF_CCU_FAIL(GroupCopy(ctx, localDst, src, ctx.goSize));
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
                RETURN_IF_CCU_FAIL(ccu::WriteReduce(ctx.arg->channels[channelId], dst[rankIdx], src, ctx.sliceSize,
                    HcclDataType::HCCL_DATA_TYPE_FP32, HcclReduceOp::HCCL_REDUCE_SUM, ctx.event, mask));
                remoteMask |= mask;
                channelId++;
            }
        }
        RETURN_IF_CCU_FAIL(ccu::EventWait(ctx.event, remoteMask));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuAllReduceMesh1DMem2MemKernel(CcuKernelArg arg)
{
    auto *kernelArg = static_cast<CcuKernelArgAllReduceMesh1DMem2Mem *>(arg);

    AllReduceMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    // 1. 初始化资源
    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::Variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    // 2. 加载参数
    uint32_t argId = 0;
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.input, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.output[ctx.arg->rankId], argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.token[ctx.arg->rankId], argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.sliceSize, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.addrOffset, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.loopParam, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.parallelParam, argId++));
    RETURN_IF_CCU_FAIL(ccu::LoadArg(ctx.goSize.residual, argId++));

    // 3. 前置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.output[ctx.arg->rankId],
            OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::WriteVariableWithNotify(ctx.arg->channels[i], ctx.token[ctx.arg->rankId],
            TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
    }

    uint32_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, allBit));
    }

    // 4. 执行算法
    RETURN_IF_CCU_FAIL(ExecuteAllReduceTransfer(ctx));

    // 5. 后置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyRecord(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::NotifyWait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID)); // 等待远端数据搬运完成
    }

    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_ar
