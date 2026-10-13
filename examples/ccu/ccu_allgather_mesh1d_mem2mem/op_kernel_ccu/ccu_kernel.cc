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

#include <algorithm>

namespace CcuAgMem2mem {

constexpr int OUTPUT_XN_ID = 1;
constexpr int TOKEN_XN_ID = 2;
constexpr int CKE_IDX_0 = 0;
constexpr int POST_SYNC_ID = 3;

static constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    return ((loop_ctx_id & set_bits(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) | (loop_iter_num_ & set_bits(loop_num_bit_num));
}

static uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
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

static uint64_t get_offset_param(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    return ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & set_bits(ms_bit_num)) << ms_shift_bit) | (cke_offset & set_bits(cke_bit_num));
}

static void InitGroupCopyResources(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr* loopSrc, ccu::local_addr* loopDst, ccu::variable* loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.ms_interleave = ccu_ms_interleave;
        ctx.moConfig.loop_count = CCU_MS_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.mem_slice = CCU_LOCAL_COPY_MS_PER_LOOP * ccu_ms_size;

        ctx.moRes.completed_event = ccu::array<ccu::event>(ctx.moConfig.loop_count);
        ctx.moRes.ccuBuf = ccu::array<ccu::ccu_buffer>(ctx.moConfig.loop_count * ctx.moConfig.ms_interleave);

        ctx.resourceAllocated = true;
    }

    std::string loopType = "localcopy";
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

constexpr uint32_t BITS_PER_EVENT = 16;

static CcuResult InitResource(AllGatherMesh1DMem2MemContext& ctx)
{
    if (ctx.arg->rankSize < 2 || ctx.arg->rankId >= ctx.arg->rankSize ||
        ctx.arg->channelCount != ctx.arg->rankSize - 1) {
        return CcuResult::CCU_E_PARA;
    }
    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    const uint32_t eventNum = (ctx.arg->rankSize + BITS_PER_EVENT - 1) / BITS_PER_EVENT;
    ctx.events.resize(eventNum);
    return CcuResult::CCU_SUCCESS;
}

static void LoadArgs(AllGatherMesh1DMem2MemContext& ctx)
{
    ccu::load_arg(ctx.input, TASK_INPUT_ADDR);
    ccu::load_arg(ctx.output[ctx.arg->rankId], TASK_OUTPUT_ADDR);
    ccu::load_arg(ctx.token[ctx.arg->rankId], TASK_TOKEN);
    ccu::load_arg(ctx.inputSliceStride, TASK_INPUT_SLICE_STRIDE);
    ccu::load_arg(ctx.outputSliceStride, TASK_OUTPUT_SLICE_STRIDE);
    ccu::load_arg(ctx.chunkSize, TASK_CHUNK_SIZE);
    ccu::load_arg(ctx.goSize.addr_offset, TASK_GO_ADDR_OFFSET);
    ccu::load_arg(ctx.goSize.loop_param_, TASK_GO_LOOP_PARAM);
    ccu::load_arg(ctx.goSize.parallel_param, TASK_GO_PARALLEL_PARAM);
    ccu::load_arg(ctx.goSize.residual, TASK_GO_RESIDUAL);
}

static void PreSync(AllGatherMesh1DMem2MemContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID);
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID);
    }
    uint16_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBit);
    }
}

static void PostSync(AllGatherMesh1DMem2MemContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID);
    }
}

static CcuResult DoAllGatherWrite(
    AllGatherMesh1DMem2MemContext& ctx, const ccu::local_addr& src_, const std::vector<ccu::remote_addr>& dst_)
{
    uint32_t channelIdx = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint32_t eventIdx = rankIdx / BITS_PER_EVENT;
        const uint16_t rankMask = 1U << (rankIdx % BITS_PER_EVENT);
        if (rankIdx == ctx.arg->rankId) {
            ccu::event_record(ctx.events[eventIdx], rankMask);
            continue;
        }
        ccu::Write(ctx.arg->channels[channelIdx], dst_[rankIdx], src_, ctx.chunkSize, ctx.events[eventIdx], rankMask);
        channelIdx++;
    }
    return CCU_SUCCESS;
}

static CcuResult DoAllGatherWait(AllGatherMesh1DMem2MemContext& ctx)
{
    const uint32_t eventNum = static_cast<uint32_t>(ctx.events.size());
    for (uint32_t eventIdx = 0; eventIdx < eventNum; eventIdx++) {
        const uint32_t remainingRanks = ctx.arg->rankSize - eventIdx * BITS_PER_EVENT;
        const uint32_t bitCount = std::min<uint32_t>(remainingRanks, BITS_PER_EVENT);
        const uint16_t eventMask = bitCount == BITS_PER_EVENT ? static_cast<uint16_t>(UINT16_MAX) :
                                                                static_cast<uint16_t>((1U << bitCount) - 1);
        ccu::event_wait(ctx.events[eventIdx], eventMask);
    }
    return CCU_SUCCESS;
}

static CcuResult DoAllGather(AllGatherMesh1DMem2MemContext& ctx)
{
    ccu::local_addr src_;
    src_.addr_ = ctx.input;
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        src_.addr_ += ctx.inputSliceStride;
    }
    src_.token = ctx.token[ctx.arg->rankId];

    ccu::local_addr localDst;
    localDst.addr_ = ctx.output[ctx.arg->rankId];
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        localDst.addr_ += ctx.outputSliceStride;
    }
    localDst.token = ctx.token[ctx.arg->rankId];

    std::vector<ccu::remote_addr> dst_(ctx.arg->rankSize);
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            continue;
        }
        dst_[rankIdx].addr_ = ctx.output[rankIdx];
        for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
            dst_[rankIdx].addr_ += ctx.outputSliceStride;
        }
        dst_[rankIdx].token = ctx.token[rankIdx];
    }

    // Preserve write -> local copy -> wait ordering.
    RETURN_IF_CCU_FAIL(DoAllGatherWrite(ctx, src_, dst_));
    RETURN_IF_CCU_FAIL(GroupCopy(ctx, localDst, src_, ctx.goSize));
    RETURN_IF_CCU_FAIL(DoAllGatherWait(ctx));
    return CCU_SUCCESS;
}

CcuResult CcuAllGatherMesh1DMem2MemKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem*>(arg);

    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernel_arg;

    RETURN_IF_CCU_FAIL(InitResource(ctx));
    LoadArgs(ctx);
    PreSync(ctx);
    CCU_IF(ctx.chunkSize != 0) { RETURN_IF_CCU_FAIL(DoAllGather(ctx)); }
    PostSync(ctx);
    return CcuResult::CCU_SUCCESS;
}

} // namespace CcuAgMem2mem
