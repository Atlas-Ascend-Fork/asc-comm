/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ALL_TO_ALL_VC_MESH1D_KERNEL_H
#define CCU_ALL_TO_ALL_VC_MESH1D_KERNEL_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_primitives.hpp>

namespace ccu = ::AscendC::ccu;

namespace ops_comm_ag {
// 规模上限与每链路 notify 数；最大单次传输 256MB，超出即拒绝
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIE_NUM = 2;

constexpr uint64_t MAX_TRANSFER_BYTES = 256ULL * 1024 * 1024;

// 传给 CCU Kernel 的编排上下文：链路句柄、rank 信息、本端收发地址/token，
// 以及按目的 rank 组织的发送字节数、发送偏移、接收偏移
struct CcuKernelArgAllToAllVCMesh1D {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
    uint64_t rankSize{0};
    uint32_t rankId{0};

    uint64_t inputAddr{0};
    uint64_t outputAddr{0};
    uint64_t inputToken{0};
    uint64_t outputToken{0};
    std::vector<uint64_t> sendBytes;
    std::vector<uint64_t> sendOffsets;
    std::vector<uint64_t> recvOffsets;
};

// Kernel 运行期上下文：把 arg 里的收发地址与偏移登记为 CCU Variable，
// 并保存各对端登记的输出地址与 token

struct AllToAllVCMesh1DContext {
    const CcuKernelArgAllToAllVCMesh1D* arg;
    ccu::variable input;
    ccu::variable inputToken;
    ccu::variable localOutput;
    ccu::variable localOutputToken;
    std::vector<ccu::variable> remoteOutput;
    std::vector<ccu::variable> remoteOutputToken;
    std::vector<ccu::variable> sendBytes;
    std::vector<ccu::variable> sendOffsets;
    std::vector<ccu::variable> recvOffsets;
    ccu::event event;
};

// 测试程序的错误检查宏：ACLCHECK / HCCLCHECK 失败时打印文件行号与返回码后提前返回

// notify 标识：OUTPUT_XN_ID/TOKEN_XN_ID 用于与对端互换输出地址与 token，POST_SYNC_ID 用于后同步
static constexpr int OUTPUT_XN_ID = 1;

static constexpr int TOKEN_XN_ID = 2;

static constexpr int CKE_IDX_0 = 0;

static constexpr int POST_SYNC_ID = 3;

// 把上下文中的常量（收发地址、token、各 rank 收发偏移）登记为 Kernel 可读的 CCU Variable
static CcuResult InitVariables(AllToAllVCMesh1DContext& ctx)
{
    ctx.input = ctx.arg->inputAddr;
    ctx.localOutput = ctx.arg->outputAddr;
    ctx.inputToken = ctx.arg->inputToken;
    ctx.localOutputToken = ctx.arg->outputToken;

    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        ctx.sendBytes[rankIdx] = ctx.arg->sendBytes[rankIdx];
        ctx.sendOffsets[rankIdx] = ctx.arg->sendOffsets[rankIdx];
        ctx.recvOffsets[rankIdx] = ctx.arg->recvOffsets[rankIdx];
    }
    return CCU_SUCCESS;
}

// 与所有对端互换接收区地址与 token：把本端对应接收偏移处的地址告知对端，
// 再等所有对端把它们的地址写回，为后续直接 write 到对端接收区做准备

static CcuResult ExchangeOutput(AllToAllVCMesh1DContext& ctx)
{
    uint32_t channelId = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId == ctx.arg->rankId) {
            continue;
        }

        ccu::variable peerDst;
        peerDst = ctx.localOutput;
        peerDst += ctx.recvOffsets[peerId];
        ccu::write_variable_with_notify(
            ctx.arg->channels[channelId], peerDst, OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID);
        ccu::write_variable_with_notify(
            ctx.arg->channels[channelId], ctx.localOutputToken, TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID);
        channelId++;
    }

    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits);
    }
    return CCU_SUCCESS;
}

// 逐目的 rank 搬运发送数据：本 rank 的数据走本地拷贝，其余走跨 rank write；
// 每段各记一个事件位，最后按全量掩码等待所有搬运完成

static CcuResult ExecuteTransfer(AllToAllVCMesh1DContext& ctx)
{
    uint32_t channelId = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint16_t mask = 1U << rankIdx;
        ccu::local_addr src;
        src.addr = ctx.input;
        src.addr += ctx.sendOffsets[rankIdx];
        src.token = ctx.inputToken;

        CCU_IF(ctx.sendBytes[rankIdx] != 0)
        {
            if (rankIdx == ctx.arg->rankId) {
                ccu::local_addr dst;
                dst.addr = ctx.localOutput;
                dst.addr += ctx.recvOffsets[rankIdx];
                dst.token = ctx.localOutputToken;
                ccu::local_copy(dst, src, ctx.sendBytes[rankIdx], ctx.event, mask);
            } else {
                ccu::remote_addr dst;
                dst.addr = ctx.remoteOutput[rankIdx];
                dst.token = ctx.remoteOutputToken[rankIdx];

                ccu::write(ctx.arg->channels[channelId], dst, src, ctx.sendBytes[rankIdx], ctx.event, mask);
            }
        }
        CCU_IF(ctx.sendBytes[rankIdx] == 0) { ccu::event_record(ctx.event, mask); }
        if (rankIdx != ctx.arg->rankId) {
            channelId++;
        }
    }

    const uint16_t allRanksMask = (1U << ctx.arg->rankSize) - 1;
    ccu::event_wait(ctx.event, allRanksMask);
    return CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排为：读取对端输出 Variable -> 登记常量 -> 互换接收地址 -> 逐 rank 搬运 -> 后同步

__ccu_host__ void CcuAllToAllVCMesh1DKernel(ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgAllToAllVCMesh1D*>(arg);
    if (kernelArg == nullptr || kernelArg->channelCount == 0) {
        return;
    }

    AllToAllVCMesh1DContext ctx;
    ctx.arg = kernelArg;
    ctx.remoteOutput.resize(kernelArg->rankSize);
    ctx.remoteOutputToken.resize(kernelArg->rankSize);
    ctx.sendBytes.resize(kernelArg->rankSize);
    ctx.sendOffsets.resize(kernelArg->rankSize);
    ctx.recvOffsets.resize(kernelArg->rankSize);

    uint32_t channelId = 0;
    // 读取各对端通道上登记的接收地址与 token
    for (uint32_t peerId = 0; peerId < kernelArg->rankSize; peerId++) {
        if (peerId != kernelArg->rankId) {
            ctx.remoteOutput[peerId] =
                ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[channelId], OUTPUT_XN_ID);
            ctx.remoteOutputToken[peerId] =
                ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[channelId], TOKEN_XN_ID);
            channelId++;
        }
    }

    // 执行编排：登记常量 -> 互换接收地址 -> 逐 rank 搬运
    InitVariables(ctx);
    ExchangeOutput(ctx);
    ExecuteTransfer(ctx);

    // 后同步：通知所有对端本 rank 已搬完，并等所有对端完成
    for (uint32_t i = 0; i < kernelArg->channelCount; i++) {
        ccu::notify_record(kernelArg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID);
    }
    for (uint32_t i = 0; i < kernelArg->channelCount; i++) {
        ccu::notify_wait(kernelArg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID);
    }
}

// AllToAllVC 通信入口：以 rankSize×rankSize 的计数矩阵描述收发（矩阵第 i 行 j 列为 i 发给 j 的元素数），
// 完成链路申请与参数准备后，通过 <<<>>> 把任务直调下发到 Stream

} // namespace ops_comm_ag

#endif // CCU_ALL_TO_ALL_VC_MESH1D_KERNEL_H
