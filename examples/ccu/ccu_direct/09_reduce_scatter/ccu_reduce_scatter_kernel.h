/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REDUCE_SCATTER_MESH1D_MEM2MEM_KERNEL_H
#define CCU_REDUCE_SCATTER_MESH1D_MEM2MEM_KERNEL_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_api_types.h>
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

// CCU 本地拷贝与调度的规模常量：最大 rank 数、Channel notify 槽位数、Die 数等
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIE_NUM = 2;

constexpr uint64_t ccu_ms_interleave = 8;

constexpr uint64_t ccu_ms_size = 4096;

constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;

constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

struct CcuKernelArgReduceScatterMesh1DMem2Mem {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
    uint64_t rankSize{0};
    uint32_t rankId{0};
};

struct loop_group_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

struct LoopGroupResource {
    uint32_t eventCount{0};
    uint32_t bufCount{0};
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
};

struct GroupOpSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

struct CcuLoopEntity {
    std::unique_ptr<ccu::func> body[2];
    std::unique_ptr<ccu::loop> loops[2];
    ccu::variable loop_param_[2];
};

struct ReduceScatterMesh1DMem2MemContext {
    const CcuKernelArgReduceScatterMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    ccu::variable inputToken;
    std::vector<ccu::variable> outputToken;
    ccu::variable rankSliceSize;
    ccu::variable sliceSize;
    ccu::event event;

    GroupOpSizeVars goSize;
    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;
    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string& loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(const std::string& loopStr) const { return loopMap.count(loopStr) != 0; }
};

static constexpr uint64_t SetBitsKernel(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum = 8;
    constexpr uint16_t ctxIdShiftBit = 45;
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 13;
    constexpr uint16_t loopNumBitNum = 13;
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBitsKernel(ctxIdBitNum)) << ctxIdShiftBit) |
           ((gsaOffset & SetBitsKernel(gsaBitNum)) << gsaShiftBit) |
           ((loopIterNum & SetBitsKernel(loopNumBitNum)) << loopNumShiftBit);
}

static uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBitsKernel(repeatBitNum)) << repeatNumShiftBit) |
           ((repeatLoopIndex & SetBitsKernel(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((totalLoopNum & SetBitsKernel(totalLoopBitNum)) << totalLoopShiftBit);
}

static uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset)
{
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 21;
    constexpr uint16_t msBitNum = 11;
    constexpr uint16_t msShiftBit = 10;
    constexpr uint16_t ckeBitNum = 10;
    constexpr uint16_t ckeShiftBit = 0;
    return ((gsaOffset & SetBitsKernel(gsaBitNum)) << gsaShiftBit) |
           ((msOffset & SetBitsKernel(msBitNum)) << msShiftBit) |
           ((ckeOffset & SetBitsKernel(ckeBitNum)) << ckeShiftBit);
}

constexpr int OUTPUT_XN_ID = 1;

constexpr int TOKEN_XN_ID = 2;

constexpr int CKE_IDX_0 = 0;

constexpr int INIT_SYNC_ID = 3;

constexpr int POST_SYNC_ID = 4;

// 声明本地拷贝要用的资源，并注册 "localcopy" 的 Loop 执行体：数据经 CcuBuffer 中转，
// 每段搬完用 event_wait 确认；两个 Loop 分别覆盖整分片和尾块两条分支。
static void InitGroupCopyResources(
    ReduceScatterMesh1DMem2MemContext& ctx, ccu::local_addr* loopSrc, ccu::local_addr* loopDst, ccu::variable* loopLen)
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
            const uint32_t bufBase = index * ctx.moConfig.ms_interleave;
            ccu::event loopEvt = ctx.moRes.completed_event[index];
            entity.body[index].reset(new ccu::func([&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                ccu::local_copy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
                ccu::local_copy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
            }));
            entity.loops[index].reset(new ccu::loop(entity.loop_param_[index], *entity.body[index]));
        }
    }
}

// 按切分参数执行本地拷贝。

static CcuResult GroupCopy(
    ReduceScatterMesh1DMem2MemContext& ctx, ccu::local_addr dst_, ccu::local_addr src_, GroupOpSizeVars goSize)
{
    ccu::local_addr loopSrc[2];
    ccu::local_addr loopDst[2];
    ccu::variable loopLen[2];

    InitGroupCopyResources(ctx, loopSrc, loopDst, loopLen);
    auto& loops = ctx.loopMap["localcopy"];

    CCU_IF(goSize.addr_offset != 0)
    {
        ccu::variable loop_param_;
        loop_param_ = GetLoopParam(0, ctx.moConfig.mem_slice * ctx.moConfig.loop_count, 0);
        loop_param_ += goSize.loop_param_;
        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;
        loopSrc[0].addr = src_.addr;
        loopSrc[0].token = src_.token;
        loopDst[0].addr = dst_.addr;
        loopDst[0].token = dst_.token;
        loopLen[0] = sliceSize;
        loops.loop_param_[0] = loop_param_;
        ccu::variable paraCfg;
        paraCfg = GetParallelParam(ctx.moConfig.loop_count - 1, 0, 1);
        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);
        std::vector<ccu::loop> grpLoops{*loops.loops[0]};
        // 构造 loop_group 即完成提交：LoopGroup 按偏移复制出多份 Loop 并发执行，无独立的提交调用
        ccu::loop_group group(paraCfg, offsetCfg, 1, grpLoops);
    }

    CCU_IF(goSize.parallel_param != 0)
    {
        src_.addr += goSize.addr_offset;
        dst_.addr += goSize.addr_offset;
        loopSrc[0].addr = src_.addr;
        loopSrc[0].token = src_.token;
        loopDst[0].addr = dst_.addr;
        loopDst[0].token = dst_.token;
        loopLen[0] = goSize.residual;
        src_.addr += goSize.residual;
        dst_.addr += goSize.residual;
        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;
        loopSrc[1].addr = src_.addr;
        loopSrc[1].token = src_.token;
        loopDst[1].addr = dst_.addr;
        loopDst[1].token = dst_.token;
        loopLen[1] = sliceSize;
        ccu::variable loopCfg0;
        loopCfg0 = GetLoopParam(0, 0, 1);
        ccu::variable loopCfg1;
        loopCfg1 = GetLoopParam(0, 0, 1);
        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);
        loops.loop_param_[0] = loopCfg0;
        loops.loop_param_[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*loops.loops[0], *loops.loops[1]};
        // 构造 loop_group 即完成提交：LoopGroup 按偏移复制出多份 Loop 并发执行，无独立的提交调用
        ccu::loop_group group(goSize.parallel_param, offsetCfg, 2, grpLoops);
    }
    return CCU_SUCCESS;
}

// 按 rank 拆分地址并执行通信：跨 rank 写入与本地拷贝配合，最后等所有 rank 完成。

static CcuResult ExecuteReduceScatterTransfer(ReduceScatterMesh1DMem2MemContext& ctx)
{
    ccu::local_addr localSrc;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> remoteDst(ctx.arg->rankSize);

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
        const uint16_t selfMask = 1U << ctx.arg->rankId;
        CCU_CHK_RET(GroupCopy(ctx, localDst, localSrc, ctx.goSize));
        CCU_CHK_RET(ccu::event_record(ctx.event, selfMask));
        CCU_CHK_RET(ccu::event_wait(ctx.event, selfMask));

        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            CCU_CHK_RET(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << INIT_SYNC_ID));
        }
        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            CCU_CHK_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << INIT_SYNC_ID));
        }

        uint32_t channelId = 0;
        uint16_t remoteMask = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1U << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                ccu::local_addr remoteRankSrc;
                remoteRankSrc.addr = ctx.input;
                for (uint64_t i = 0; i < rankIdx; i++) {
                    remoteRankSrc.addr += ctx.rankSliceSize;
                }
                remoteRankSrc.token = ctx.inputToken;
                CCU_CHK_RET(ccu::write_reduce(
                    ctx.arg->channels[channelId], remoteDst[rankIdx], remoteRankSrc, ctx.sliceSize, HCCL_DATA_TYPE_FP32,
                    HCCL_REDUCE_SUM, ctx.event, mask));
                remoteMask |= mask;
                channelId++;
            }
        }
        CCU_CHK_RET(ccu::event_wait(ctx.event, remoteMask));
    }
    return CcuResult::CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排三阶段：前同步互换地址 -> 执行通信 -> 后同步确认完成。

__ccu_host__ void CcuReduceScatterMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4, uint64_t sqeArgs5, uint64_t sqeArgs6,
    uint64_t sqeArgs7, uint64_t sqeArgs8, uint64_t sqeArgs9, uint64_t sqeArgs10, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgReduceScatterMesh1DMem2Mem*>(arg);
    ReduceScatterMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;
    if (ctx.arg->channelCount == 0) {
        return;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.outputToken.resize(ctx.arg->rankSize);
    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.outputToken[peerId] =
                ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    uint32_t argId = 0;
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.input, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.output[ctx.arg->rankId], argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.inputToken, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.outputToken[ctx.arg->rankId], argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.rankSliceSize, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.sliceSize, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.addr_offset, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.loop_param_, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.parallel_param, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.residual, argId++));

    // 前同步：本端 output 地址与 token 写入对端并置位对端 notify，
    // 再等待对端把其地址与 token 写回本端，避免读到旧值
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID));
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.outputToken[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID));
    }
    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }

    CCU_CHK_VOID_RET(ExecuteReduceScatterTransfer(ctx));

    // 后同步：告知所有对端本 rank 搬运完成，并等所有对端完成
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
}

} // namespace ops_comm_ag

#endif // CCU_REDUCE_SCATTER_MESH1D_MEM2MEM_KERNEL_H
