/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_BROADCAST_MESH1D_KERNEL_H
#define CCU_BROADCAST_MESH1D_KERNEL_H

#include <algorithm>
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

// 规模上限与每链路 notify 数
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIE_NUM = 2;

// 传给 CCU Kernel 的编排上下文：链路句柄、rank 信息与根 rank
struct CcuKernelArgBroadcastMesh1D {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
    uint64_t rankSize{0};
    uint32_t rankId{0};
    uint32_t rootId{0};
};

// Kernel 运行期上下文：本端缓冲/token/数据量，以及各对端登记的缓冲地址与 token

struct BroadcastMesh1DContext {
    const CcuKernelArgBroadcastMesh1D* arg;
    ccu::variable localBuffer;
    ccu::variable localToken;
    ccu::variable data_size;
    std::vector<ccu::variable> remoteBuffer;
    std::vector<ccu::variable> remoteToken;
    ccu::event event;
};

// 测试程序的错误检查宏：ACLCHECK / HCCLCHECK 失败时打印文件行号与返回码后提前返回

// notify 标识：BUFFER_XN_ID/TOKEN_XN_ID 用于与对端互换缓冲地址与 token，POST_SYNC_ID 用于后同步
constexpr int BUFFER_XN_ID = 1;

constexpr int TOKEN_XN_ID = 2;

constexpr int CKE_IDX_0 = 0;

constexpr int POST_SYNC_ID = 3;

constexpr uint64_t MAX_TRANSFER_BYTES = 256ULL * 1024 * 1024;

// 与所有对端互换缓冲地址与 token：先把本端地址写给对端，再等对端写回
static CcuResult ExchangeBuffers(BroadcastMesh1DContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.localBuffer, BUFFER_XN_ID, CKE_IDX_0, 1U << BUFFER_XN_ID));
        CCU_CHK_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.localToken, TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID));
    }

    const uint32_t allBits = (1U << BUFFER_XN_ID) | (1U << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }
    return CCU_SUCCESS;
}

// 由根 rank 把本端数据写到其余所有 rank 的缓冲；非根 rank 直接返回

static CcuResult BroadcastFromRoot(BroadcastMesh1DContext& ctx)
{
    if (ctx.arg->rankId != ctx.arg->rootId) {
        return CCU_SUCCESS;
    }

    ccu::local_addr src_;
    src_.addr = ctx.localBuffer;
    src_.token = ctx.localToken;

    uint32_t channelId = 0;
    uint16_t remoteMask = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            continue;
        }

        const uint16_t mask = 1U << rankIdx;
        ccu::remote_addr dst_;
        dst_.addr = ctx.remoteBuffer[rankIdx];
        dst_.token = ctx.remoteToken[rankIdx];
        CCU_CHK_RET(ccu::write(ctx.arg->channels[channelId], dst_, src_, ctx.data_size, ctx.event, mask));
        remoteMask |= mask;
        channelId++;
    }
    CCU_CHK_RET(ccu::event_wait(ctx.event, remoteMask));
    return CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排为：读取对端缓冲 Variable -> 加载任务参数 -> 互换缓冲地址 -> 根 rank 广播 -> 后同步

__ccu_host__ void CcuBroadcastMesh1DKernel(uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgBroadcastMesh1D*>(arg);
    BroadcastMesh1DContext ctx;
    ctx.arg = kernelArg;
    if (ctx.arg->channelCount == 0) {
        return;
    }

    // 读取各对端通道上登记的缓冲地址与 token
    ctx.remoteBuffer.resize(ctx.arg->rankSize);
    ctx.remoteToken.resize(ctx.arg->rankSize);
    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.remoteBuffer[peerId] =
                ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelId], BUFFER_XN_ID);
            ctx.remoteToken[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelId], TOKEN_XN_ID);
            channelId++;
        }
    }

    // 按 SQE 顺序加载任务参数，load_arg 必须在程序最开始时调用
    uint32_t argId = 0;
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.localBuffer, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.localToken, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.data_size, argId++));

    // 互换缓冲地址 -> 根 rank 广播
    CCU_CHK_VOID_RET(ExchangeBuffers(ctx));
    CCU_CHK_VOID_RET(BroadcastFromRoot(ctx));

    // 后同步：通知所有对端本 rank 已完成，并等所有对端完成
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
}

// Broadcast 通信入口：把 rootRank 的数据广播到所有 rank，
// 完成链路申请与参数准备后，通过 <<<>>> 把任务直调下发到 Stream

} // namespace ops_comm_ag

#endif // CCU_BROADCAST_MESH1D_KERNEL_H
