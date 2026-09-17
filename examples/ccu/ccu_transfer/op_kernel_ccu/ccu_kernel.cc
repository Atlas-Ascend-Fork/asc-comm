/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms of conditions of
 * CANN Open Software License Agreement Version 2.0 ("License").
 * Please refer to the License for details.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_kernel.h"

namespace ops_hccl_transfer {
namespace {

constexpr uint32_t OUTPUT_XN_ID = 1;
constexpr uint32_t TOKEN_XN_ID = 2;
constexpr uint32_t CKE_IDX_0 = 0;
constexpr uint32_t POST_SYNC_ID = 3;
constexpr uint16_t EVENT_MASK = 1;

struct TransferContext {
    const CcuTransferKernelArg* arg = nullptr;
    ccu::variable input;
    ccu::variable inputToken;
    ccu::variable output;
    ccu::variable outputToken;
    ccu::variable dataSize;
    ccu::variable remoteOutput;
    ccu::variable remoteOutputToken;
    ccu::event event;
};

ccu::local_addr MakeLocalAddr(const ccu::variable& address, const ccu::variable& token)
{
    ccu::local_addr addr;
    addr.addr_ = address;
    addr.token = token;
    return addr;
}

ccu::remote_addr MakeRemoteAddr(const ccu::variable& address, const ccu::variable& token)
{
    ccu::remote_addr addr;
    addr.addr_ = address;
    addr.token = token;
    return addr;
}

CcuResult ExchangeRemoteOutput(TransferContext& ctx)
{
    RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.output, OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID));
    RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.outputToken, TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID));

    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
    RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, allBits));
    return CCU_SUCCESS;
}

CcuResult ExecuteLocalCopy(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    RETURN_IF_CCU_FAIL(ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

CcuResult ExecuteWrite(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::remote_addr output = MakeRemoteAddr(ctx.remoteOutput, ctx.remoteOutputToken);

    RETURN_IF_CCU_FAIL(ccu::write(ctx.arg->channels[0], output, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(
        ccu::write(ctx.arg->channels[0], output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

CcuResult ExecuteWriteReduce(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::remote_addr output = MakeRemoteAddr(ctx.remoteOutput, ctx.remoteOutputToken);

    RETURN_IF_CCU_FAIL(ccu::write_reduce(
        ctx.arg->channels[0], output, input, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event,
        EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

} // namespace

CcuResult CcuTransferKernel(ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuTransferKernelArg*>(arg);
    if (kernelArg == nullptr || kernelArg->rankSize != 2U || kernelArg->channelCount != 1U) {
        return CCU_E_PARA;
    }

    TransferContext ctx{};
    ctx.arg = kernelArg;
    ctx.remoteOutput = ccu::GetResByChannel<ccu::variable>(kernelArg->channels[0], OUTPUT_XN_ID);
    ctx.remoteOutputToken = ccu::GetResByChannel<ccu::variable>(kernelArg->channels[0], TOKEN_XN_ID);

    uint32_t argId = 0;
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.input, argId++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.output, argId++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.inputToken, argId++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.outputToken, argId++));
    RETURN_IF_CCU_FAIL(ccu::load_arg(ctx.dataSize, argId++));

    RETURN_IF_CCU_FAIL(ExchangeRemoteOutput(ctx));

#if SCENARIO_NUM == 1
    RETURN_IF_CCU_FAIL(ExecuteLocalCopy(ctx));
#elif SCENARIO_NUM == 2
    RETURN_IF_CCU_FAIL(ExecuteWrite(ctx));
#elif SCENARIO_NUM == 3
    RETURN_IF_CCU_FAIL(ExecuteWriteReduce(ctx));
#else
#error "SCENARIO_NUM must be 1, 2, or 3"
#endif

    RETURN_IF_CCU_FAIL(ccu::notify_record(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::notify_wait(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID));
    return CCU_SUCCESS;
}

} // namespace ops_hccl_transfer
