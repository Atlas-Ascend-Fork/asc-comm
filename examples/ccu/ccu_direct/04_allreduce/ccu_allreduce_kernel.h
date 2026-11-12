/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ALLREDUCE_MESH1D_MEM2MEM_KERNEL_H
#define CCU_ALLREDUCE_MESH1D_MEM2MEM_KERNEL_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_primitives.hpp>

namespace ccu = ::AscendC::ccu;

namespace ops_comm_ag {
// CCU 本地拷贝与调度的规模常量：最大 rank 数、Channel notify 槽位数、Die 数等
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIE_NUM = 2;

// Host 向 Kernel 传入的 CCU 通信参数：对端 Channel 句柄、rank 数量与本 rankId
struct CcuKernelArgAllReduceMesh1DMem2Mem {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
    uint64_t rankSize{0};
    uint32_t rankId{0};
};

// Kernel 侧运行期状态：输入/输出/token 变量、切分大小与完成事件

struct AllReduceMesh1DMem2MemContext {
    const CcuKernelArgAllReduceMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    // SC2：各对端经通道登记的输入地址与 token（gather 读取对端输入用）
    std::vector<ccu::variable> remoteInput;
    std::vector<ccu::variable> remoteInputToken;
    ccu::variable sliceSize;
    ccu::event event;
};

constexpr int OUTPUT_XN_ID = 1;

constexpr int TOKEN_XN_ID = 2;

constexpr int CKE_IDX_0 = 0;

constexpr int INIT_SYNC_ID = 3;

constexpr int POST_SYNC_ID = 4;

// SC2（gather + local_reduce）交换对端输入地址所用的通道变量槽位与 CKE 掩码位：
// 通道仅预留 4 个变量槽（下标 0-3），槽 1/2 已被 output/token 占用，对端输入地址与
// token 使用空闲槽 0/3；CKE[0] 掩码位 1/2/3/4 已被占用，登记完成信号使用空闲的 5/6 位
constexpr int REMOTE_INPUT_XN_ID = 3;

constexpr int REMOTE_INPUT_TOKEN_XN_ID = 0;

constexpr int REMOTE_INPUT_XN_MASK_BIT = 5;

constexpr int REMOTE_INPUT_TOKEN_MASK_BIT = 6;

// 把本 rank 输入先拷入本端输出，再 write_reduce 到各对端输出并等待完成
static CcuResult ExecuteAllReduceTransfer(AllReduceMesh1DMem2MemContext& ctx)
{
    ccu::local_addr src_;
    ccu::local_addr localDst;
    std::vector<ccu::remote_addr> dst_;

    dst_.resize(ctx.arg->rankSize);

    src_.addr = ctx.input;
    src_.token = ctx.token[ctx.arg->rankId];

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            localDst.addr = ctx.output[rankIdx];
            localDst.token = ctx.token[rankIdx];
        } else {
            dst_[rankIdx].addr = ctx.output[rankIdx];
            dst_[rankIdx].token = ctx.token[rankIdx];
        }
    }

    CCU_IF(ctx.sliceSize != 0)
    {
        const uint16_t selfMask = 1U << ctx.arg->rankId;
        ccu::local_copy(localDst, src_, ctx.sliceSize, ctx.event, selfMask);
        ccu::event_wait(ctx.event, selfMask);

        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << INIT_SYNC_ID);
        }
        for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
            ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << INIT_SYNC_ID);
        }

        uint32_t channelId = 0;
        uint16_t remoteMask = 0;
        for (uint64_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
            const uint16_t mask = 1U << rankIdx;
            if (rankIdx != ctx.arg->rankId) {
                ccu::write_reduce(
                    ctx.arg->channels[channelId], dst_[rankIdx], src_, ctx.sliceSize, HCCL_DATA_TYPE_FP32,
                    HCCL_REDUCE_SUM, ctx.event, mask);
                remoteMask |= mask;
                channelId++;
            }
        }
        ccu::event_wait(ctx.event, remoteMask);
    }

    return CcuResult::CCU_SUCCESS;
}

// SC2 前同步：仅交换本端输入地址与 token（gather 需要读取对端输入；
// SC2 无远端写，不交换 output 地址），并等待所有对端完成登记

static CcuResult ExchangeRemoteInput(AllReduceMesh1DMem2MemContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.input, REMOTE_INPUT_XN_ID, CKE_IDX_0, 1U << REMOTE_INPUT_XN_MASK_BIT);
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], REMOTE_INPUT_TOKEN_XN_ID, CKE_IDX_0,
            1U << REMOTE_INPUT_TOKEN_MASK_BIT);
    }

    const uint32_t allBits = (1U << REMOTE_INPUT_XN_MASK_BIT) | (1U << REMOTE_INPUT_TOKEN_MASK_BIT);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits);
    }
    return CcuResult::CCU_SUCCESS;
}

// SC2：gather + local_reduce，独立的"聚合-归约"AllReduce 实现路径（与场景1 的
// write_reduce 散射归约殊途同归，输出同为全 rank 逐元素和）：
//   1. 本端 input 拷入 bufs[rankId]；
//   2. 逐个对端 read（对端片上内存 -> bufs[对端 rankId]），把各 rank 数据搬运到本地；
//   3. local_reduce 的 N 路 ccu_buffer 重载把 bufs[0..N-1] 归约到 bufs[0]；
//   4. bufs[0] 拷回本端 output。
// 约束：ccu_buffer 归约重载支持 2~8 路，单片缓存 4096 字节

static CcuResult ExecuteGatherLocalReduce(AllReduceMesh1DMem2MemContext& ctx)
{
    const uint64_t rankSize = ctx.arg->rankSize;
    if (rankSize < 2U || rankSize > 8U) {
        return CcuResult::CCU_E_NOT_SUPPORT;
    }

    ccu::array<ccu::ccu_buffer> bufs(static_cast<uint32_t>(rankSize));

    // 1) 本端贡献落入 bufs[rankId]
    ccu::local_addr src;
    src.addr = ctx.input;
    src.token = ctx.token[ctx.arg->rankId];
    ccu::local_copy(bufs[ctx.arg->rankId], src, ctx.sliceSize, ctx.event, 1U);
    ccu::event_wait(ctx.event, 1U);

    // 2) gather：逐个对端 read（对端片上内存 -> 本端 ccu_buffer）
    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < rankSize; peerId++) {
        if (peerId == ctx.arg->rankId) {
            continue;
        }
        ccu::remote_addr remote;
        remote.addr = ctx.remoteInput[peerId];
        remote.token = ctx.remoteInputToken[peerId];
        ccu::read(
            ctx.arg->channels[channelIdx], bufs[static_cast<uint32_t>(peerId)], remote, ctx.sliceSize, ctx.event, 1U);
        ccu::event_wait(ctx.event, 1U);
        channelIdx++;
    }

    // 3) N 路 ccu_buffer 归约：bufs[0] = SUM(bufs[0..N-1])，结果落在 bufs[0]
    ccu::local_reduce(
        bufs.data(), static_cast<uint32_t>(rankSize), HcclDataType::HCCL_DATA_TYPE_FP32,
        HcclDataType::HCCL_DATA_TYPE_FP32, HcclReduceOp::HCCL_REDUCE_SUM, ctx.sliceSize, ctx.event, 1U);
    ccu::event_wait(ctx.event, 1U);

    // 4) 结果拷回本端 output
    ccu::local_addr dst;
    dst.addr = ctx.output[ctx.arg->rankId];
    dst.token = ctx.token[ctx.arg->rankId];
    ccu::local_copy(dst, bufs[0], ctx.sliceSize, ctx.event, 1U);
    ccu::event_wait(ctx.event, 1U);

    return CcuResult::CCU_SUCCESS;
}

__ccu_host__ void CcuAllReduceMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgAllReduceMesh1DMem2Mem*>(arg);
    AllReduceMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    if (ctx.arg->channelCount == 0) {
        return;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);
#if SCENARIO_NUM == 2
    ctx.remoteInput.resize(ctx.arg->rankSize);
    ctx.remoteInputToken.resize(ctx.arg->rankSize);
#endif

    uint32_t channelIdx = 0;
    for (uint64_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
#if SCENARIO_NUM == 2
            // SC2：读取各对端经通道登记的输入地址与 token（槽 0/3）
            ctx.remoteInput[peerId] =
                ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], REMOTE_INPUT_XN_ID);
            ctx.remoteInputToken[peerId] =
                ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], REMOTE_INPUT_TOKEN_XN_ID);
#else
            ctx.output[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
#endif
            channelIdx++;
        }
    }

    // 按 SQE 声明顺序加载任务参数，load_arg 必须在程序最开始时调用
    uint32_t argId = 0;
    ccu::load_arg(ctx.input, argId++);
    ccu::load_arg(ctx.output[ctx.arg->rankId], argId++);
    ccu::load_arg(ctx.token[ctx.arg->rankId], argId++);
    ccu::load_arg(ctx.sliceSize, argId++);

#if SCENARIO_NUM == 2
    // SC2 前同步：仅交换本端输入地址与 token（gather 读取对端输入需要；
    // SC2 无远端写，不交换 output 地址）
    ExchangeRemoteInput(ctx);

    // SC2：gather + local_reduce 的独立"聚合-归约"流程，输出与场景1 相同
    ExecuteGatherLocalReduce(ctx);
#else
    // 前同步：本端 output 地址与 token 写入对端并置位对端 notify，
    // 再等待对端把其地址与 token 写回本端，避免读到旧值
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID);
        ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID);
    }

    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits);
    }

    ExecuteAllReduceTransfer(ctx);
#endif

    // 后同步：告知所有对端本 rank 搬运完成，并等所有对端完成
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID);
    }
}

} // namespace ops_comm_ag

#endif // CCU_ALLREDUCE_MESH1D_MEM2MEM_KERNEL_H
