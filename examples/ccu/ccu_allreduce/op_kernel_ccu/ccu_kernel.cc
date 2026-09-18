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

static CcuResult ExecuteAllReduceTransfer(AllReduceMesh1DMem2MemContext& ctx)
{
    ccu::local_addr src_;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> dst_;

    dst_.resize(ctx.arg->rankSize);

    src_.addr_ = ctx.input;
    src_.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr_ = ctx.output[rankIdx];
            localDst.token = ctx.token[rankIdx];
        } else {
            dst_[rankIdx].addr_ = ctx.output[rankIdx];
            dst_[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        const uint16_t selfMask = 1 << ctx.arg->rankId;
        RETURN_IF_CCU_FAIL(ccu::LocalCopy(localDst, src_, ctx.sliceSize, ctx.event, selfMask));
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
                RETURN_IF_CCU_FAIL(ccu::WriteReduce(
                    ctx.arg->channels[channelId], dst_[rankIdx], src_, ctx.sliceSize, HcclDataType::HCCL_DATA_TYPE_FP32,
                    HcclReduceOp::HCCL_REDUCE_SUM, ctx.event, mask_));
                remoteMask |= mask_;
                channelId++;
            }
        }
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, remoteMask));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuAllReduceMesh1DMem2MemKernel(ccu_kernel_arg arg)
{
    auto* kernel_arg = static_cast<CcuKernelArgAllReduceMesh1DMem2Mem*>(arg);

    AllReduceMesh1DMem2MemContext ctx;
    ctx.arg = kernel_arg;

    if (ctx.arg->channelCount == 0) {
        return CcuResult::CCU_E_INTERNAL;
    }

    // 1. 初始化资源
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

    // 2. 加载参数
    uint32_t arg_id_ = 0;
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.input, arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.output[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.token[ctx.arg->rankId], arg_id_++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.sliceSize, arg_id_++));

    // 3. 前置同步
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

    // 4. 执行算法
    RETURN_IF_CCU_FAIL(ExecuteAllReduceTransfer(ctx));

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

} // namespace ops_hccl_ar
