/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REDUCE_SCATTER_MESH1D_DEMO_H
#define CCU_REDUCE_SCATTER_MESH1D_DEMO_H

#include "ccu/hcomm/ccu_primitives.hpp"
#include "ccu/hcomm/ccu_api_types.h"

#include <cstdint>
#include <climits>
#include <memory>
#include <vector>

namespace ccu = ::AscendC::ccu;
#include <string>
#include <set>

// ============================================================================
// 常量定义
// ============================================================================

constexpr uint32_t RS_MAX_RANK_SIZE = 16;

constexpr int RS_INPUT_XN_ID = 0;
constexpr int RS_SCRATCH_XN_ID = 1;
constexpr int RS_TOKEN_XN_ID = 2;
constexpr int RS_POST_SYNC_ID = 3;

constexpr int RS_CKE_IDX_0 = 0;

constexpr uint64_t RS_CCU_MS_SIZE = 4096;
constexpr uint64_t RS_CCU_MS_INTERLEAVE = 8;
constexpr uint64_t RS_CCU_MS_DEFAULT_LOOP_COUNT = 64;

// ============================================================================
// 参数结构体
// ============================================================================

struct ReduceScatterKernelArg {
    uint64_t rankSize;
    uint32_t rankId;
    ChannelHandle channels[RS_MAX_RANK_SIZE];
    uint32_t channelCount;
    HcclDataType data_type_;
    HcclDataType outputDataType;
    HcclReduceOp reduce_op;
};

struct GroupOpSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

// ============================================================================
// 运行时配置（对应 CcuKernelAlgBase::moConfig / moRes）
// ============================================================================

struct loop_group_config {
    uint64_t ms_interleave;
    uint64_t loop_count;
    uint64_t mem_slice;
};

struct LoopGroupResource {
    // 真正分配延后到 AllocGoResource：默认构造为空 array(0)，运行期再 move-assign 出
    // 实际容量，避免在 ReduceScatterContext 默认构造时就一次性占用大量底层资源。
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
    uint32_t eventCount{0};
    uint32_t bufCount{0};
};

// ============================================================================
// Bit-packing 辅助函数（对应 ccu_kernel_utils.cc）
// ============================================================================

static inline constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static inline uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loop_num_bit_num = 12;
    return set_bits(loop_num_bit_num);
}

static inline uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    constexpr uint16_t loop_num_shift_bit = 0;
    return ((loop_ctx_id & set_bits(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((loop_iter_num_ & set_bits(loop_num_bit_num)) << loop_num_shift_bit);
}

static inline uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & set_bits(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & set_bits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & set_bits(total_loop_bit_num)) << total_loop_shift_bit);
}

static inline uint64_t get_offset_param(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    constexpr uint16_t cke_shift_bit = 0;
    return ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & set_bits(ms_bit_num)) << ms_shift_bit) |
           ((cke_offset & set_bits(cke_bit_num)) << cke_shift_bit);
}

static inline uint64_t GetExpansionParam(uint64_t expansionNum)
{
    constexpr uint64_t expansionNum2 = 2;
    constexpr uint64_t expansionNumShiftBit = 53;
    return (expansionNum == expansionNum2 ? uint64_t(1) : uint64_t(2)) << expansionNumShiftBit;
}

static inline uint32_t GetReduceExpansionNum(
    HcclReduceOp reduce_op, HcclDataType data_type_, HcclDataType outputDataType)
{
    (void)reduce_op;
    (void)data_type_;
    (void)outputDataType;
    // 简化实现：大多数场景 expansionNum = 1
    // 完整实现需要 DataTypeSizeGet(outputDataType) / DataTypeSizeGet(data_type_)
    return 1;
}

// ============================================================================
// CalGoSize（对应 CcuKernelAlgBase::CalGoSize，host 侧调用）
// ============================================================================

static inline std::vector<uint64_t> CalGoSize(uint64_t size, const loop_group_config& config)
{
    uint64_t loopSize = config.loop_count * config.mem_slice;
    uint64_t maxSize = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.mem_slice;
    uint64_t p = size - m * loopSize - n * config.mem_slice;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loop_count - 1;
        p = config.mem_slice;
    }

    uint64_t offset = config.mem_slice * config.loop_count * m;
    uint64_t loop_iter_num_ = m;

    uint64_t loopExtendNum = 0;
    uint64_t tailSize = 0;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = get_parallel_param(n - 1, 0, 1);
        tailSize = config.mem_slice;
    } else if (n == 0 && p != 0) {
        loopExtendNum = get_parallel_param(0, 0, 1);
        tailSize = p;
    } else {
        loopExtendNum = get_parallel_param(n - 1, 1, 2);
        tailSize = p;
    }

    return {offset, loop_iter_num_, loopExtendNum, tailSize};
}

// ============================================================================
// AllocGoResource（对应 CcuKernelAlgBase::AllocGoResource）
// ============================================================================

static inline CcuResult AllocGoResource(
    loop_group_config& config, LoopGroupResource& res, bool& allocated,
    uint32_t parallelDim = RS_CCU_MS_DEFAULT_LOOP_COUNT, uint32_t msPerLoop = 1)
{
    if (allocated) {
        return CCU_SUCCESS;
    }

    config.ms_interleave = RS_CCU_MS_INTERLEAVE;
    config.loop_count = parallelDim;
    config.mem_slice = msPerLoop * RS_CCU_MS_SIZE;

    res.eventCount = config.loop_count;
    res.completed_event = ccu::array<ccu::event>(res.eventCount);

    res.bufCount = config.loop_count * config.ms_interleave;
    res.ccuBuf = ccu::array<ccu::ccu_buffer>(res.bufCount);

    allocated = true;
    return CCU_SUCCESS;
}

// ============================================================================
// InitResource
// ============================================================================

struct ReduceScatterContext {
    const ReduceScatterKernelArg* arg;

    ccu::variable input[RS_MAX_RANK_SIZE];
    ccu::variable scratch[RS_MAX_RANK_SIZE];
    ccu::variable token[RS_MAX_RANK_SIZE];
    ccu::variable output;
    ccu::variable currentRankSliceInputOffset;
    ccu::variable currentRankSliceOutputOffset;
    ccu::variable normalSliceSize;
    ccu::variable lastSliceSize;
    ccu::variable inputRepeatStride;
    ccu::variable outputRepeatStride;
    ccu::variable repeat_num;
    ccu::variable flag;
    GroupOpSizeVars goSize;

    uint16_t selfBit;
    uint16_t allBit;

    ccu::local_addr myInput;
    ccu::remote_addr remoteInput[RS_MAX_RANK_SIZE];
    ccu::local_addr scratchMem[RS_MAX_RANK_SIZE];
    ccu::event event;

    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated;

    // 同一 ccu::loop 跨 loop_group 复用：body 只翻译一次（在 ccu::loop ctor 里），
    // 每个 group 进入前通过 reduceLoopParam[i] 注入不同的 var_-based 参数。
    std::unique_ptr<ccu::func> reduceBody[2];
    std::unique_ptr<ccu::loop> reduceLoops[2];
    ccu::variable reduceLoopParam[2];
    bool loopRegistered;

    // loop body 中的外部 local_addr（每个 loop index 各两组）
    ccu::local_addr loopDst[2];
    ccu::local_addr loopSrc[2];
    ccu::local_addr loopScratch[2][RS_MAX_RANK_SIZE];
    ccu::variable loopLen[2];
    ccu::variable loopLenExp[2];
};

static CcuResult InitResource(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;
    uint32_t channelIdx = 0;

    if (arg->channelCount == 0) {
        return CcuResult::CCU_E_PARA;
    }

    // ctx 默认构造时 variable / local_addr / remote_addr / event 成员已自动 alloc，
    // 这里仅需把"远端 rank"对应的 variable 通过 GetResByChannel 重绑定即可
    // (右值赋值走 variable 的 move-assign，直接覆盖 handle)。
    for (uint64_t peerId = 0; peerId < arg->rankSize; peerId++) {
        if (peerId != arg->rankId) {
            ctx.input[peerId] = ccu::GetResByChannel<ccu::variable>(arg->channels[channelIdx], RS_INPUT_XN_ID);
            ctx.scratch[peerId] = ccu::GetResByChannel<ccu::variable>(arg->channels[channelIdx], RS_SCRATCH_XN_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::variable>(arg->channels[channelIdx], RS_TOKEN_XN_ID);
            channelIdx++;
        }
    }

    ctx.selfBit = 1 << arg->rankId;
    ctx.allBit = ((1 << arg->rankSize) - 1) & (~(1 << arg->rankId));

    // LoopEngine 池由各 ccu::loop_group 在 ctor 第二个参数 max_loop_num 处自报，
    // kernel 按需扩容（取最大值，跨组复用）。下面两个 group 分别填 1 / 2。

    ctx.resourceAllocated = false;
    ctx.loopRegistered = false;

    return CCU_SUCCESS;
}

// ============================================================================
// LoadArgs
// ============================================================================

static CcuResult LoadArgs(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;

    uint32_t arg_id_ = 0;
    CCU_CHK_RET(ccu::load_arg(ctx.input[arg->rankId], arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.output, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.token[arg->rankId], arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.scratch[arg->rankId], arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.currentRankSliceInputOffset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.currentRankSliceOutputOffset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.inputRepeatStride, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.outputRepeatStride, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.normalSliceSize, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.lastSliceSize, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.repeat_num, arg_id_++));

    CCU_CHK_RET(ccu::load_arg(ctx.goSize.addr_offset, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.loop_param_, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.parallel_param, arg_id_++));
    CCU_CHK_RET(ccu::load_arg(ctx.goSize.residual, arg_id_++));

    return CCU_SUCCESS;
}

// ============================================================================
// PreSync / PostSync
// ============================================================================

static void PreSync(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;

    for (uint32_t i = 0; i < arg->channelCount; i++) {
        ccu::write_variable_with_notify(
            arg->channels[i], ctx.input[arg->rankId], RS_INPUT_XN_ID, RS_CKE_IDX_0, 1 << RS_INPUT_XN_ID);
        ccu::write_variable_with_notify(
            arg->channels[i], ctx.scratch[arg->rankId], RS_SCRATCH_XN_ID, RS_CKE_IDX_0, 1 << RS_SCRATCH_XN_ID);
        ccu::write_variable_with_notify(
            arg->channels[i], ctx.token[arg->rankId], RS_TOKEN_XN_ID, RS_CKE_IDX_0, 1 << RS_TOKEN_XN_ID);
    }

    uint32_t allBit = (1 << RS_INPUT_XN_ID) | (1 << RS_SCRATCH_XN_ID) | (1 << RS_TOKEN_XN_ID);
    for (uint32_t i = 0; i < arg->channelCount; i++) {
        ccu::notify_wait(arg->channels[i], RS_CKE_IDX_0, allBit);
    }
}

static void PostSync(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;

    for (uint32_t i = 0; i < arg->channelCount; i++) {
        ccu::notify_record(arg->channels[i], RS_CKE_IDX_0, 1 << RS_POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < arg->channelCount; i++) {
        ccu::notify_wait(arg->channels[i], RS_CKE_IDX_0, 1 << RS_POST_SYNC_ID);
    }
}

// ============================================================================
// CreateReduceLoop
// ============================================================================

static CcuResult CreateReduceLoop(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;
    const uint32_t size = arg->rankSize;

    constexpr uint32_t LOOP_NUM = 16;
    CCU_CHK_RET(AllocGoResource(ctx.moConfig, ctx.moRes, ctx.resourceAllocated, LOOP_NUM));

    if (ctx.loopRegistered) {
        return CCU_SUCCESS;
    }

    uint32_t expansionNum = GetReduceExpansionNum(arg->reduce_op, arg->data_type, arg->output_data_type);
    uint32_t usedBufNum = size > expansionNum ? size : expansionNum;
    (void)expansionNum;
    (void)usedBufNum;

    for (int32_t index = 0; index < 2; index++) {
        // ctx.loopDst / loopSrc / loopScratch / loopLen / loopLenExp 已在 ctx 默认构造时 alloc
        uint32_t bufBase = static_cast<uint32_t>(index * ctx.moConfig.ms_interleave);

        ccu::event loopEvt = ctx.moRes.completed_event[index];

        // 用对象式 API：func 承载 body 闭包，loop ctor 立即把 body 翻译为 loop_block；
        // var_-based 参数预留 ctx.reduceLoopParam[index]，由 ReduceLoopGroup 在加入
        // 不同 loop_group 之前赋值。
        ctx.reduceBody[index].reset(new ccu::func([&ctx, index, bufBase, loopEvt, size, arg]() {
            // mask_ 已与 event 解耦：由各个数据 / 同步 API 末尾参数显式传入。
            for (uint32_t i = 0; i < size; i++) {
                const uint32_t copyMask = 1 << i;
                if (i == arg->rankId) {
                    ccu::LocalCopy(
                        ctx.moRes.ccuBuf[bufBase + i], ctx.loopSrc[index], ctx.loopLen[index], loopEvt, copyMask);
                } else {
                    ccu::LocalCopy(
                        ctx.moRes.ccuBuf[bufBase + i], ctx.loopScratch[index][i], ctx.loopLen[index], loopEvt,
                        copyMask);
                }
            }
            ccu::event_wait(loopEvt, (1 << size) - 1);

            if (size > 1) {
                ccu::LocalReduce(
                    &ctx.moRes.ccuBuf[bufBase], size, arg->data_type, arg->output_data_type, arg->reduce_op,
                    ctx.loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
            }

            ccu::LocalCopy(ctx.loopDst[index], ctx.moRes.ccuBuf[bufBase], ctx.loopLenExp[index], loopEvt, 1);
            ccu::event_wait(loopEvt, 1);
        }));
        ctx.reduceLoops[index].reset(new ccu::loop(ctx.reduceLoopParam[index], *ctx.reduceBody[index]));
    }

    ctx.loopRegistered = true;
    return CCU_SUCCESS;
}

// ============================================================================
// ReduceLoopGroup
// ============================================================================

static CcuResult ReduceLoopGroup(
    ReduceScatterContext& ctx, ccu::local_addr outDstOrg, ccu::local_addr srcOrg, ccu::local_addr* scratchOrg,
    uint32_t scratchCount, GroupOpSizeVars& goSize)
{
    const auto* arg = ctx.arg;
    const uint32_t size = scratchCount;

    ccu::local_addr dst_;
    dst_.addr_ = outDstOrg.addr_;
    dst_.token = outDstOrg.token;

    ccu::local_addr src_;
    src_.addr_ = srcOrg.addr_;
    src_.token = srcOrg.token;

    ccu::local_addr scratch[RS_MAX_RANK_SIZE];
    for (uint32_t idx = 0; idx < size; idx++) {
        scratch[idx].addr_ = scratchOrg[idx].addr_;
        scratch[idx].token = scratchOrg[idx].token;
    }

    CCU_CHK_RET(CreateReduceLoop(ctx));

    uint32_t expansionNum = GetReduceExpansionNum(arg->reduce_op, arg->data_type, arg->output_data_type);
    ccu::variable sliceSizeExpansion;

    if (expansionNum != 1) {
        ccu::variable tmp;
        tmp = GetExpansionParam(expansionNum);
        dst_.token = dst_.token + tmp;
    }

    // m 部分
    CCU_IF(goSize.loop_param_ != 0)
    {
        ccu::variable loop_param_;
        loop_param_ = get_loop_param(0, ctx.moConfig.mem_slice * ctx.moConfig.loop_count, 0);
        loop_param_ = loop_param_ + goSize.loop_param_;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;
        sliceSizeExpansion = ctx.moConfig.mem_slice * expansionNum;

        // 绑定 loop0 的外部 local_addr 和 variable
        ctx.loopDst[0].addr_ = dst_.addr_;
        ctx.loopDst[0].token = dst_.token;
        ctx.loopSrc[0].addr_ = src_.addr_;
        ctx.loopSrc[0].token = src_.token;
        for (uint32_t i = 0; i < size; i++) {
            ctx.loopScratch[0][i].addr_ = scratch[i].addr_;
            ctx.loopScratch[0][i].token = scratch[i].token;
        }
        ctx.loopLen[0] = sliceSize;
        ctx.loopLenExp[0] = sliceSizeExpansion;

        ccu::variable paraCfg;
        paraCfg = get_parallel_param(ctx.moConfig.loop_count - 1, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = get_offset_param(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        // 把本组要用的 var_-based loop 参数注入：reduceLoopParam[0] 已被 reduceLoops[0]
        // 持引用，给它赋值即在当前位置 emit 一条 LoadImd/AddVar，并在 loop_group 翻译时
        // 转化为对应 loop_instr 的低位参数。
        ctx.reduceLoopParam[0] = loop_param_;
        std::vector<ccu::loop> grpLoops{*ctx.reduceLoops[0]};
        ccu::loop_group group(paraCfg, offsetCfg, /* max_loop_num= */ 1, grpLoops);
    }

    // n+p 部分
    CCU_IF(goSize.parallel_param != 0)
    {
        for (uint32_t i = 0; i < size; i++) {
            scratch[i].addr_ += goSize.addr_offset;
        }
        src_.addr_ += goSize.addr_offset;
        for (uint32_t i = 0; i < expansionNum; i++) {
            dst_.addr_ += goSize.addr_offset;
        }

        sliceSizeExpansion = 0;
        for (uint32_t i = 0; i < expansionNum; i++) {
            sliceSizeExpansion = sliceSizeExpansion + goSize.residual;
        }

        // 绑定 loop0 参数 (p 部分)
        ctx.loopDst[0].addr_ = dst_.addr_;
        ctx.loopDst[0].token = dst_.token;
        ctx.loopSrc[0].addr_ = src_.addr_;
        ctx.loopSrc[0].token = src_.token;
        for (uint32_t i = 0; i < size; i++) {
            ctx.loopScratch[0][i].addr_ = scratch[i].addr_;
            ctx.loopScratch[0][i].token = scratch[i].token;
        }
        ctx.loopLen[0] = goSize.residual;
        ctx.loopLenExp[0] = sliceSizeExpansion;

        // n 部分偏移
        for (uint32_t i = 0; i < size; i++) {
            scratch[i].addr_ += goSize.residual;
        }
        src_.addr_ += goSize.residual;
        for (uint32_t i = 0; i < expansionNum; i++) {
            dst_.addr_ += goSize.residual;
        }

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;
        sliceSizeExpansion = ctx.moConfig.mem_slice * expansionNum;

        // 绑定 loop1 参数 (n 部分)
        ctx.loopDst[1].addr_ = dst_.addr_;
        ctx.loopDst[1].token = dst_.token;
        ctx.loopSrc[1].addr_ = src_.addr_;
        ctx.loopSrc[1].token = src_.token;
        for (uint32_t i = 0; i < size; i++) {
            ctx.loopScratch[1][i].addr_ = scratch[i].addr_;
            ctx.loopScratch[1][i].token = scratch[i].token;
        }
        ctx.loopLen[1] = sliceSize;
        ctx.loopLenExp[1] = sliceSizeExpansion;

        ccu::variable loopCfg0;
        loopCfg0 = get_loop_param(0, 0, 1);

        ccu::variable loopCfg1;
        loopCfg1 = get_loop_param(0, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = get_offset_param(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        ctx.reduceLoopParam[0] = loopCfg0;
        ctx.reduceLoopParam[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*ctx.reduceLoops[0], *ctx.reduceLoops[1]};
        ccu::loop_group group(goSize.parallel_param, offsetCfg, /* max_loop_num= */ 2, grpLoops);
    }

    return CCU_SUCCESS;
}

// ============================================================================
// DoReduceScatter
// ============================================================================

static CcuResult DoReduceScatter(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;
    uint32_t channelId = 0;

    ccu::local_addr myOutput;
    myOutput.addr_ = ctx.output;
    myOutput.addr_ += ctx.currentRankSliceOutputOffset;
    myOutput.token = ctx.token[arg->rankId];

    ccu::variable sliceSize;
    sliceSize = (arg->rankId == (arg->rankSize - 1)) ? ctx.lastSliceSize : ctx.normalSliceSize;

    CCU_IF(sliceSize != 0)
    {
        // mask_ 已与 event 解耦：record/read 用 (1 << rankIdx)，等待用全位 mask_。
        for (uint32_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
            const uint32_t rankMask = 1 << rankIdx;
            if (rankIdx == arg->rankId) {
                ccu::event_record(ctx.event, rankMask);
            } else {
                ccu::Read(
                    arg->channels[channelId], ctx.scratchMem[rankIdx], ctx.remoteInput[rankIdx], sliceSize, ctx.event,
                    rankMask);
                channelId++;
            }
        }

        ccu::event_wait(ctx.event, (1 << arg->rankSize) - 1);

        ReduceLoopGroup(ctx, myOutput, ctx.myInput, ctx.scratchMem, arg->rankSize, ctx.goSize);
    }

    return CCU_SUCCESS;
}

// ============================================================================
// DoRepeatReduceScatter
// ============================================================================

static CcuResult DoRepeatReduceScatter(ReduceScatterContext& ctx)
{
    const auto* arg = ctx.arg;

    ccu::variable scratchOffset;
    scratchOffset = 0;

    for (uint32_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
        if (rankIdx == arg->rankId) {
            ctx.myInput.addr_ = ctx.input[rankIdx];
            ctx.myInput.addr_ += ctx.currentRankSliceInputOffset;
            ctx.myInput.token = ctx.token[rankIdx];
        } else {
            ctx.remoteInput[rankIdx].addr_ = ctx.input[rankIdx];
            ctx.remoteInput[rankIdx].addr_ += ctx.currentRankSliceInputOffset;
            ctx.remoteInput[rankIdx].token = ctx.token[rankIdx];
        }

        ctx.scratchMem[rankIdx].addr_ = ctx.scratch[arg->rankId];
        ctx.scratchMem[rankIdx].addr_ += scratchOffset;
        scratchOffset = scratchOffset + ctx.normalSliceSize;
        ctx.scratchMem[rankIdx].token = ctx.token[arg->rankId];
    }

    ccu::variable repeatNumAdd;
    repeatNumAdd = 1;
    ctx.flag = 0;

    CCU_DO
    {
        ctx.repeat_num = ctx.repeat_num + repeatNumAdd;

        CCU_IF(ctx.flag == 1)
        {
            for (uint64_t rankIdx = 0; rankIdx < arg->rankSize; rankIdx++) {
                if (rankIdx == arg->rankId) {
                    ctx.myInput.addr_ += ctx.inputRepeatStride;
                } else {
                    ctx.remoteInput[rankIdx].addr_ += ctx.inputRepeatStride;
                }
            }
            ctx.output = ctx.output + ctx.outputRepeatStride;
        }

        DoReduceScatter(ctx);
        ctx.flag = 1;
    }
    CCU_WHILE(ctx.repeat_num != UINT64_MAX);

    return CCU_SUCCESS;
}

// ============================================================================
// 主入口 Kernel 函数
// ============================================================================

CcuResult CcuReduceScatterMesh1dKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<ReduceScatterKernelArg*>(arg);

    ReduceScatterContext ctx;
    ctx.arg = kernel_arg;
    ctx.selfBit = 0;
    ctx.allBit = 0;
    ctx.resourceAllocated = false;
    ctx.loopRegistered = false;
    ctx.moConfig.ms_interleave = 0;
    ctx.moConfig.loop_count = 0;
    ctx.moConfig.mem_slice = 0;
    ctx.moRes.eventCount = 0;
    ctx.moRes.bufCount = 0;

    // LoopEngine 池由各 loop_group ctor 自动按 max_loop_num 扩容，无需在此显式申请。

    CCU_CHK_RET(InitResource(ctx));
    CCU_CHK_RET(LoadArgs(ctx));

    PreSync(ctx);

    CCU_CHK_RET(DoRepeatReduceScatter(ctx));

    PostSync(ctx);

    return CCU_SUCCESS;
}

#endif // CCU_REDUCE_SCATTER_MESH1D_DEMO_H
