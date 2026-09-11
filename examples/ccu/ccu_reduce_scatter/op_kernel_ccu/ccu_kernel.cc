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

static constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
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
    constexpr uint16_t cke_shift_bit = 0;
    return ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & set_bits(ms_bit_num)) << ms_shift_bit) |
           ((cke_offset & set_bits(cke_bit_num)) << cke_shift_bit);
}

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
        ccu::loop_group group(paraCfg, offsetCfg, 1, grpLoops);
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
        ccu::loop_group group(goSize.parallel_param, offsetCfg, 2, grpLoops);
    }

    return CCU_SUCCESS;
}

static CcuResult ExecuteReduceScatterTransfer(ReduceScatterMesh1DMem2MemContext& ctx)
{
    ccu::local_addr localSrc;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> remoteDst;

    remoteDst.resize(ctx.arg->rankSize);

    localSrc.addr_ = ctx.input;
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        localSrc.addr_ += ctx.rankSliceSize;
    }
    localSrc.token = ctx.inputToken;

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr_ = ctx.output[rankIdx];
            localDst.token = ctx.outputToken[rankIdx];
        } else {
            remoteDst[rankIdx].addr_ = ctx.output[rankIdx];
            remoteDst[rankIdx].token = ctx.outputToken[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        const uint16_t selfMask = 1 << ctx.arg->rankId;
        // 先用本 rank 输入中的第 rankId 段初始化本地输出。
        RETURN_IF_CCU_FAIL(GroupCopy(ctx, localDst, localSrc, ctx.goSize));
        RETURN_IF_CCU_FAIL(ccu::event_record(ctx.event, selfMask));
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, selfMask));

        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1 << INIT_SYNC_ID));
        }
        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1 << INIT_SYNC_ID));
        }

        uint32_t channelId = 0;
        uint16_t remoteMask = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask_ = 1 << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                // 第 rankIdx 段只归约到 rankIdx 的输出，而不是广播完整输入。
                ccu::local_addr remoteRankSrc;
                remoteRankSrc.addr_ = ctx.input;
                for (uint64_t i = 0; i < rankIdx; i++) {
                    remoteRankSrc.addr_ += ctx.rankSliceSize;
                }
                remoteRankSrc.token = ctx.inputToken;
                RETURN_IF_CCU_FAIL(ccu::WriteReduce(
                    ctx.arg->channels[channelId], remoteDst[rankIdx], remoteRankSrc, ctx.sliceSize,
                    HcclDataType::HCCL_DATA_TYPE_FP32, HcclReduceOp::HCCL_REDUCE_SUM, ctx.event, mask_));
                remoteMask |= mask_;
                channelId++;
            }
        }
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, remoteMask));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuReduceScatterMesh1DMem2MemKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<CcuKernelArgReduceScatterMesh1DMem2Mem*>(arg);

    ReduceScatterMesh1DMem2MemContext ctx;
    ctx.arg = kernel_arg;

    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    // 1. 初始化资源
    ctx.output.resize(ctx.arg->rankSize);
    ctx.outputToken.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.outputToken[peerId] = ccu::GetResByChannel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    // 2. 加载参数
    uint32_t arg_id_ = 0;
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.input, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.output[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.inputToken, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.outputToken[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.rankSliceSize, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.sliceSize, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.addr_offset, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.loop_param_, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.parallel_param, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.goSize.residual, arg_id_++));

    // 3. 前置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1 << OUTPUT_XN_ID));
        RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.outputToken[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1 << TOKEN_XN_ID));
    }

    uint32_t allBit = (1 << OUTPUT_XN_ID) | (1 << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBit));
    }

    // 4. 执行算法
    RETURN_IF_CCU_FAIL(ExecuteReduceScatterTransfer(ctx));

    // 5. 后置同步
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        RETURN_IF_CCU_FAIL(
            ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1 << POST_SYNC_ID)); // 等待远端数据搬运完成
    }

    return CcuResult::CCU_SUCCESS;
}

} // namespace ops_hccl_rs
