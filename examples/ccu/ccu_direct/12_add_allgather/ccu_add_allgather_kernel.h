/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ADD_ALLGATHER_KERNEL_H
#define CCU_ADD_ALLGATHER_KERNEL_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_primitives.hpp>

namespace ccu = ::AscendC::ccu;

namespace ops_comm_ag {

#define CCU_CHK_RET(call)                                \
    do {                                                 \
        CcuResult ccuRet = static_cast<CcuResult>(call); \
        if (ccuRet != CCU_SUCCESS) {                     \
            return ccuRet;                               \
        }                                                \
    } while (0)

#define CCU_CHK_VOID_RET(call)                           \
    do {                                                 \
        CcuResult ccuRet = static_cast<CcuResult>(call); \
        if (ccuRet != CCU_SUCCESS) {                     \
            return;                                      \
        }                                                \
    } while (0)

constexpr uint64_t CCU_BUF_INTERLEAVE = 8;
constexpr uint64_t CCU_BUF_SIZE = 4096;
constexpr uint32_t CCU_LOCAL_COPY_BUF_PER_LOOP = 8;
constexpr uint32_t CCU_BUF_LOCAL_COPY_LOOP_COUNT = 8;
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIRECT_TASK_ARG_NUM = 8;
constexpr uint32_t EVENT_NUM = 2;
constexpr uint32_t MAPPED_VARIABLE_NUM = 2;
constexpr uint32_t CCU_DIE_NUM = 2;

constexpr uint32_t SIZE_TABLE[HCCL_DATA_TYPE_RESERVED] = {
    sizeof(int8_t),
    sizeof(int16_t),
    sizeof(int32_t),
    2,
    sizeof(float),
    sizeof(int64_t),
    sizeof(uint64_t),
    sizeof(uint8_t),
    sizeof(uint16_t),
    sizeof(uint32_t),
    8,
    2,
    16,
    2,
    1,
    1,
    1,
    1};

struct LoopGroupConfig {
    uint32_t ccuBufInterleave;
    uint32_t loopCount;
    uint64_t ccuBuf;
};

struct LoopGroupResource {
    ccu::array<ccu::event> completedEvent{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
    uint32_t eventCount;
    uint32_t bufCount;
};

struct GroupOpSizeVars {
    ccu::variable addrOffset;
    ccu::variable loopParam;
    ccu::variable parallelParam;
    ccu::variable residual;
};

struct CcuLoopEntity {
    std::unique_ptr<ccu::func> body[2];
    std::unique_ptr<ccu::loop> loops[2];
    ccu::variable loopParam[2];
};

struct CcuKernelArgAllGatherMesh1DMem2Mem {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t channelCount;
    uint64_t rankSize;
    uint32_t rankId;
    ccu_variable_handle varHandle;
    uint32_t varNum;
    ccu_event_handle eventHandle;
    uint32_t eventNum;
};

struct CcuMappedAddrs {
    uint64_t eventVa[EVENT_NUM];
    uint64_t startVa[MAPPED_VARIABLE_NUM];
};

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    ccu::variable currentRankSliceInputOffset;
    ccu::variable currentRankSliceOutputOffset;
    ccu::variable sliceSize;
    ccu::event event;

    GroupOpSizeVars goSize;
    LoopGroupConfig moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(std::string loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(std::string loopStr) { return loopMap.count(loopStr) != 0; }
};

// 把 CCU 接口返回的 CcuResult 归一化为 HCCL 错误码，便于统一用 HCCLCHECK 检查

static constexpr uint64_t SetBits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loopNumBitNum = 12;
    return SetBits(loopNumBitNum);
}

static uint64_t GetParallelParamForHost(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit) |
           ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

// 将总字节数拆为 LoopGroup 调度参数：整轮循环 + 尾部残余。
// 返回 {已搬偏移, 循环轮数, 尾部并行参数, 尾部字节数}。
static std::vector<uint64_t> CalGoSize(uint64_t size, const LoopGroupConfig& config)
{
    uint64_t loopSize = config.loopCount * config.ccuBuf;
    uint64_t maxSize = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.ccuBuf;
    uint64_t p = size - m * loopSize - n * config.ccuBuf;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loopCount - 1;
        p = config.ccuBuf;
    }

    uint64_t offset = config.ccuBuf * config.loopCount * m;
    uint64_t loopIterNum = m;

    uint64_t loopExtendNum = 0;
    uint64_t tailSize = 0;
    constexpr uint64_t loopNumTwo = 2;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = GetParallelParamForHost(n - 1, 0, 1);
        tailSize = config.ccuBuf;
    } else if (n == 0 && p != 0) {
        loopExtendNum = GetParallelParamForHost(0, 0, 1);
        tailSize = p;
    } else {
        loopExtendNum = GetParallelParamForHost(n - 1, 1, loopNumTwo);
        tailSize = p;
    }

    return {offset, loopIterNum, loopExtendNum, tailSize};
}

constexpr int OUTPUT_VAR_ID = 1;
constexpr int TOKEN_VAR_ID = 2;
constexpr int EVENT_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t addrOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum = 8;
    constexpr uint16_t ctxIdShiftBit = 45;
    constexpr uint16_t addrBitNum = 32;
    constexpr uint16_t addrShiftBit = 13;
    constexpr uint16_t loopNumBitNum = 13;
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit) |
           ((addrOffset & SetBits(addrBitNum)) << addrShiftBit) |
           ((loopIterNum & SetBits(loopNumBitNum)) << loopNumShiftBit);
}

static uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit) |
           ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

static uint64_t GetOffsetParam(uint64_t addrOffset, uint64_t ccuBufOffset, uint64_t eventOffset)
{
    constexpr uint16_t addrBitNum = 32;
    constexpr uint16_t addrShiftBit = 21;
    constexpr uint16_t ccuBufBitNum = 11;
    constexpr uint16_t ccuBufShiftBit = 10;
    constexpr uint16_t eventBitNum = 10;
    constexpr uint16_t eventShiftBit = 0;
    return ((addrOffset & SetBits(addrBitNum)) << addrShiftBit) |
           ((ccuBufOffset & SetBits(ccuBufBitNum)) << ccuBufShiftBit) |
           ((eventOffset & SetBits(eventBitNum)) << eventShiftBit);
}

// 声明本地拷贝要用的资源，并注册 "localcopy" 的 Loop 执行体：数据经 CcuBuffer 中转，
// 每段搬完用 event_wait 确认；两个 Loop 分别覆盖整分片和尾块两条分支。
static void InitGroupCopyResources(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr* loopSrc, ccu::local_addr* loopDst, ccu::variable* loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.ccuBufInterleave = CCU_BUF_INTERLEAVE;
        ctx.moConfig.loopCount = CCU_BUF_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.ccuBuf = CCU_LOCAL_COPY_BUF_PER_LOOP * CCU_BUF_SIZE;

        ctx.moRes.eventCount = ctx.moConfig.loopCount;
        ctx.moRes.completedEvent = ccu::array<ccu::event>(ctx.moRes.eventCount);

        ctx.moRes.bufCount = ctx.moConfig.loopCount * ctx.moConfig.ccuBufInterleave;
        ctx.moRes.ccuBuf = ccu::array<ccu::ccu_buffer>(ctx.moRes.bufCount);

        ctx.resourceAllocated = true;
    }

    std::string loopType = "localcopy";
    if (!ctx.IsLoopEntityRegistered(loopType)) {
        ctx.CreateLoopEntity(loopType);
        auto& entity = ctx.loopMap[loopType];
        for (uint32_t index = 0; index < 2; index++) {
            uint32_t bufBase = index * ctx.moConfig.ccuBufInterleave;
            ccu::event loopEvt = ctx.moRes.completedEvent[index];
            entity.body[index].reset(new ccu::func([&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                ccu::local_copy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
                ccu::local_copy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
            }));
            entity.loops[index].reset(new ccu::loop(entity.loopParam[index], *entity.body[index]));
        }
    }
}

// 按切分参数执行本地拷贝。
static CcuResult GroupCopy(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr dst, ccu::local_addr src, GroupOpSizeVars goSize)
{
    ccu::local_addr loopSrc[2];
    ccu::local_addr loopDst[2];
    ccu::variable loopLen[2];

    InitGroupCopyResources(ctx, loopSrc, loopDst, loopLen);

    auto& loops = ctx.loopMap["localcopy"];

    CCU_IF(goSize.addrOffset != 0)
    {
        ccu::variable loopParam;
        loopParam = GetLoopParam(0, ctx.moConfig.ccuBuf * ctx.moConfig.loopCount, 0);
        loopParam += goSize.loopParam;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.ccuBuf;

        loopSrc[0].addr = src.addr;
        loopSrc[0].token = src.token;
        loopDst[0].addr = dst.addr;
        loopDst[0].token = dst.token;
        loopLen[0] = sliceSize;

        loops.loopParam[0] = loopParam;
        ccu::variable paraCfg;
        paraCfg = GetParallelParam(ctx.moConfig.loopCount - 1, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.ccuBuf, ctx.moConfig.ccuBufInterleave, 1);
        std::vector<ccu::loop> grpLoops{*loops.loops[0]};
        ccu::loop_group group(paraCfg, offsetCfg, ctx.moConfig.loopCount, grpLoops);
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

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.ccuBuf;

        loopSrc[1].addr = src.addr;
        loopSrc[1].token = src.token;
        loopDst[1].addr = dst.addr;
        loopDst[1].token = dst.token;
        loopLen[1] = sliceSize;

        ccu::variable loopCfg0;
        loopCfg0 = GetLoopParam(0, 0, 1);
        ccu::variable loopCfg1;
        loopCfg1 = GetLoopParam(0, 0, 1);
        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.ccuBuf, ctx.moConfig.ccuBufInterleave, 1);

        loops.loopParam[0] = loopCfg0;
        loops.loopParam[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*loops.loops[0], *loops.loops[1]};
        ccu::loop_group group(goSize.parallelParam, offsetCfg, ctx.moConfig.loopCount, grpLoops);
    }

    return CCU_SUCCESS;
}

// 按 rank 拆分地址并执行 AllGather：跨 rank 写入与本地拷贝配合，最后等所有 rank 完成。
static CcuResult ExecuteAllGatherTransfer(AllGatherMesh1DMem2MemContext& ctx)
{
    ccu::local_addr src;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> dst;

    dst.resize(ctx.arg->rankSize);

    src.addr = ctx.input;
    src.addr += ctx.currentRankSliceInputOffset;
    src.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr = ctx.output[rankIdx];
            localDst.addr += ctx.currentRankSliceOutputOffset;
            localDst.token = ctx.token[rankIdx];
        } else {
            dst[rankIdx].addr = ctx.output[rankIdx];
            dst[rankIdx].addr += ctx.currentRankSliceOutputOffset;
            dst[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        uint32_t channelId = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                CCU_CHK_RET(
                    ccu::write(ctx.arg->channels[channelId], dst[rankIdx], src, ctx.sliceSize, ctx.event, mask));
                channelId++;
            }
        }
    }

    CCU_CHK_RET(GroupCopy(ctx, localDst, src, ctx.goSize));
    CCU_CHK_RET(ccu::event_record(ctx.event, 1 << ctx.arg->rankId));

    const uint16_t totalMask = (1 << ctx.arg->rankSize) - 1;
    CCU_CHK_RET(ccu::event_wait(ctx.event, totalMask));

    return CcuResult::CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排三阶段：前同步互换地址 -> 执行通信 -> 后同步确认完成。
__ccu_host__ void CcuAllGatherMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4, uint64_t sqeArgs5, uint64_t sqeArgs6,
    uint64_t sqeArgs7, uint64_t sqeArgs8, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem*>(arg);

    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    if (ctx.arg->channelCount == 0) {
        return;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_VAR_ID);
            ctx.token[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_VAR_ID);
            channelIdx++;
        }
    }

    uint32_t argId = 0;
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.token[ctx.arg->rankId], argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.currentRankSliceInputOffset, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.currentRankSliceOutputOffset, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.sliceSize, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.addrOffset, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.loopParam, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.parallelParam, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.residual, argId++));

    ccu::array<ccu::event> events(ctx.arg->eventHandle, ctx.arg->eventNum);
    ccu::event_wait(events[0]);

    ccu::array<ccu::variable> vars(ctx.arg->varHandle, ctx.arg->varNum);
    ctx.input = vars[0];
    ctx.output[ctx.arg->rankId] = vars[1];

    // 前同步：本端 output 地址与 token 写入对端并置位对端 notify，
    // 再等待对端把其地址与 token 写回本端，避免读到旧值
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_VAR_ID, EVENT_IDX_0, 1 << OUTPUT_VAR_ID));
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_VAR_ID, EVENT_IDX_0, 1 << TOKEN_VAR_ID));
    }

    uint32_t allBit = (1 << OUTPUT_VAR_ID) | (1 << TOKEN_VAR_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], EVENT_IDX_0, allBit));
    }

    CCU_CHK_VOID_RET(ExecuteAllGatherTransfer(ctx));

    // 后同步：告知所有对端本 rank 搬运完成，并等所有对端完成
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_record(ctx.arg->channels[i], EVENT_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], EVENT_IDX_0, 1 << POST_SYNC_ID));
    }
    ccu::event_record(events[1]);
}
} // namespace ops_comm_ag

#endif // CCU_ADD_ALLGATHER_KERNEL_H
