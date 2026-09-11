/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_groupcopy_demo.h"

CcuResult AgCreateMultiOpCopy(AllGatherContext& ctx)
{
    CCU_CHK_RET(AgAllocGoResource(
        ctx.moConfig, ctx.moRes, ctx.resourceAllocated, AG_CCU_MS_LOCAL_COPY_LOOP_COUNT, AG_LOCAL_COPY_MS_PER_LOOP));

    if (ctx.groupCopyRegistered) {
        return CCU_SUCCESS;
    }

    for (uint32_t index = 0; index < 2; index++) {
        ccu::event loopEvt = ctx.moRes.completed_event[index];
        ccu::ccu_buffer& buf = ctx.moRes.ccuBuf[index * ctx.moConfig.ms_interleave];

        ctx.copyBody[index].reset(new ccu::func([&ctx, index, buf, loopEvt]() {
            ccu::LocalCopy(buf, ctx.loopSrc[index], ctx.loopLen[index], loopEvt, 1);
            ccu::event_wait(loopEvt, 1);
            ccu::LocalCopy(ctx.loopDst[index], buf, ctx.loopLen[index], loopEvt, 1);
            ccu::event_wait(loopEvt, 1);
        }));
        ctx.copyLoops[index].reset(new ccu::loop(ctx.copyLoopParam[index], *ctx.copyBody[index]));
    }

    ctx.groupCopyRegistered = true;
    return CCU_SUCCESS;
}

CcuResult AgGroupCopy(AllGatherContext& ctx, ccu::local_addr dst_, ccu::local_addr src_, GroupCopyGoSizeVars& goSize)
{
    CCU_CHK_RET(AgCreateMultiOpCopy(ctx));

    // 第一个 loop_group：搬运 m 部分（256K 整数倍部分），单 loop 模板按 loop_count 展开。
    CCU_IF(goSize.addr_offset != 0)
    {
        ccu::variable loop_param_;
        loop_param_ = AgGetLoopParam(0, ctx.moConfig.mem_slice * ctx.moConfig.loop_count, 0);
        loop_param_ += goSize.loop_param_;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;

        ctx.loopSrc[0].addr_ = src_.addr_;
        ctx.loopSrc[0].token = src_.token;
        ctx.loopDst[0].addr_ = dst_.addr_;
        ctx.loopDst[0].token = dst_.token;
        ctx.loopLen[0] = sliceSize;

        ccu::variable paraCfg;
        paraCfg = AgGetParallelParam(ctx.moConfig.loop_count - 1, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = AgGetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        ctx.copyLoopParam[0] = loop_param_;
        std::vector<ccu::loop> grpLoops{*ctx.copyLoops[0]};
        ccu::loop_group group(paraCfg, offsetCfg, /* max_loop_num= */ 1, grpLoops);
    }

    // 第二个 loop_group：搬运 n + p 部分。
    CCU_IF(goSize.parallel_param != 0)
    {
        src_.addr_ += goSize.addr_offset;
        dst_.addr_ += goSize.addr_offset;

        ctx.loopSrc[0].addr_ = src_.addr_;
        ctx.loopSrc[0].token = src_.token;
        ctx.loopDst[0].addr_ = dst_.addr_;
        ctx.loopDst[0].token = dst_.token;
        ctx.loopLen[0] = goSize.residual;

        src_.addr_ += goSize.residual;
        dst_.addr_ += goSize.residual;

        ctx.loopSrc[1].addr_ = src_.addr_;
        ctx.loopSrc[1].token = src_.token;
        ctx.loopDst[1].addr_ = dst_.addr_;
        ctx.loopDst[1].token = dst_.token;
        ctx.loopLen[1] = ctx.moConfig.mem_slice;

        ccu::variable loopCfg0;
        loopCfg0 = AgGetLoopParam(0, 0, 1);

        ccu::variable loopCfg1;
        loopCfg1 = AgGetLoopParam(0, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = AgGetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        ctx.copyLoopParam[0] = loopCfg0;
        ctx.copyLoopParam[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*ctx.copyLoops[0], *ctx.copyLoops[1]};
        ccu::loop_group group(goSize.parallel_param, offsetCfg, /* max_loop_num= */ 2, grpLoops);
    }

    return CCU_SUCCESS;
}
