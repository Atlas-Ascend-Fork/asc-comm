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
#include "ccu/hcomm/ccu_control_flow_macro.h"

namespace ops_hccl_transfer {
namespace {

constexpr uint32_t OUTPUT_XN_ID = 1;
constexpr uint32_t TOKEN_XN_ID = 2;
constexpr uint32_t REMOTE_INPUT_XN_ID = 5;
constexpr uint32_t REMOTE_INPUT_TOKEN_XN_ID = 6;
constexpr uint32_t CKE_IDX_0 = 0;
constexpr uint32_t INIT_SYNC_ID = 4;
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
    ccu::variable remoteInput;
    ccu::variable remoteInputToken;
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
    RETURN_IF_CCU_FAIL(
        ccu::write_variable_with_notify(ctx.arg->channels[0], ctx.output, OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID));
    RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.outputToken, TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID));
#if SCENARIO_NUM == 5 || SCENARIO_NUM == 6
    // read / read_reduce 场景需要读取对端输入，额外交换对端输入地址
    RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.input, REMOTE_INPUT_XN_ID, CKE_IDX_0, 1U << REMOTE_INPUT_XN_ID));
    RETURN_IF_CCU_FAIL(ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.inputToken, REMOTE_INPUT_TOKEN_XN_ID, CKE_IDX_0, 1U << REMOTE_INPUT_TOKEN_XN_ID));
    const uint32_t allBits =
        (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID) | (1U << REMOTE_INPUT_XN_ID) | (1U << REMOTE_INPUT_TOKEN_XN_ID);
#else
    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
#endif
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

// 场景 7：CCU_DO/CCU_WHILE/CCU_ELSE 软件控制流宏 + local_notify 跨 kernel 同步。
// 三段拷贝与 SCENARIO 1 相同，但用软件控制流宏组织：
//   - CCU_DO { ... } CCU_WHILE(cond) 循环执行搬运直至计数达到；
//   - CCU_IF(cond) { ... } CCU_ELSE { ... } 分支选择搬运通路；
//   - event_record("tag", mask)/event_wait("tag", mask) 为 local_notify 重载（同 die 跨 kernel）。
CcuResult ExecuteControlFlowAndLocalNotify(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    // 循环计数 variable（由 load_arg 装入的 dataSize 不可直接比较立即数，
    // 这里用固定循环 1 次的 do-while + CCU_IF/CCU_ELSE 演示控制流）
    ccu::variable loopCounter;
    ccu::variable loopLimit;
    ccu::variable one;
    loopCounter = 0;
    loopLimit = 1;
    one = 1;

    // local_notify 生产侧：向同 die 的后续 kernel 发出阶段完成信号（tag 字符串配对）
    ccu::event_record("transfer_ready", 0x1);

    // CCU_DO + CCU_WHILE：do-while 循环（body 至少执行一次，条件满足后退出）
    CCU_DO
    {
        RETURN_IF_CCU_FAIL(ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK));
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
        loopCounter = loopCounter + one;
    }
    CCU_WHILE(loopCounter != loopLimit);

    // CCU_IF + CCU_ELSE：分支选择（恒真走 then 分支，演示 else 存在）
    CCU_IF(loopCounter == loopLimit)
    {
        RETURN_IF_CCU_FAIL(ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK));
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    }
    CCU_ELSE
    {
        // 不会执行到（loopCounter == loopLimit 恒真），保留以演示 CCU_ELSE 语法
        RETURN_IF_CCU_FAIL(ccu::local_copy(stagingBuffer, output, ctx.dataSize, ctx.event, EVENT_MASK));
        RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    }

    RETURN_IF_CCU_FAIL(ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));

    // local_notify 消费侧：等待同 die 先序 kernel 的阶段完成信号（tag 字符串配对）
    ccu::event_wait("transfer_phase_done", 0x1);
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
    RETURN_IF_CCU_FAIL(ccu::write(ctx.arg->channels[0], output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

CcuResult ExecuteWriteReduce(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteOutput = MakeRemoteAddr(ctx.remoteOutput, ctx.remoteOutputToken);

    RETURN_IF_CCU_FAIL(ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::write_reduce(
        ctx.arg->channels[0], remoteOutput, input, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event,
        EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

// 场景 4：load/store + address 演示。
// load 从片上内存地址取 uint64 值装入 variable，store 为其反向操作；
// address 是独立的地址资源类型，支持 imm/var/addr+var 赋值，
// 此处用 address 承载输入地址并装入 local_addr，演示其与 variable 的互换性。
CcuResult ExecuteLoadStore(TransferContext& ctx)
{
    // 1) address 资源：variable 间接赋值后作为 local_addr 的地址成员
    //    （local_addr.addr_ 为 address 类型，local_addr.token 为 variable 类型）
    ccu::address inputAddr;
    inputAddr = ctx.input; // address = variable（间接寻址地址）

    ccu::local_addr input;
    input.addr_ = inputAddr;
    input.token = ctx.inputToken; // token 成员为 variable，直接变量赋值

    // 2) variable 间接寻址装载：load(var, var) 与 store(var, var)
    ccu::variable srcAddr;
    srcAddr = ctx.input;
    ccu::variable dstAddr;
    dstAddr = ctx.output;
    ccu::variable loadedByVar;
    RETURN_IF_CCU_FAIL(ccu::load(srcAddr, loadedByVar));
    RETURN_IF_CCU_FAIL(ccu::store(dstAddr, loadedByVar));

    // 3) 搬运校验：address 组装的 local_addr 完成完整 input->output 拷贝
    ccu::address outputAddr;
    outputAddr = ctx.output;

    ccu::local_addr output;
    output.addr_ = outputAddr;
    output.token = ctx.outputToken;

    RETURN_IF_CCU_FAIL(ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

// 场景 5：read 演示（对端写 -> 本端读回，注意 read 参数序为"目的在前，源在后"）。
CcuResult ExecuteRead(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteInput = MakeRemoteAddr(ctx.remoteInput, ctx.remoteInputToken);

    // mem2mem：本端 output <- 对端 remoteInput
    RETURN_IF_CCU_FAIL(ccu::read(ctx.arg->channels[0], output, remoteInput, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    // buffer：本端 ccu_buffer <- 对端 remoteInput
    RETURN_IF_CCU_FAIL(ccu::read(ctx.arg->channels[0], stagingBuffer, remoteInput, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    // stagingBuffer 落回 output，验证 buffer 通路读到的数据同样正确
    RETURN_IF_CCU_FAIL(ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    return CCU_SUCCESS;
}

// 场景 6：read_reduce 演示（对端写 -> 本端读取并原地归约）。
CcuResult ExecuteReadReduce(TransferContext& ctx)
{
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteInput = MakeRemoteAddr(ctx.remoteInput, ctx.remoteInputToken);

    // output 先写入初值（对端输入值），read_reduce 后 = 初值 + 对端值（FP32 SUM）
    RETURN_IF_CCU_FAIL(ccu::local_copy(output, MakeLocalAddr(ctx.input, ctx.inputToken), ctx.dataSize, ctx.event,
        EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::event_wait(ctx.event, EVENT_MASK));
    RETURN_IF_CCU_FAIL(ccu::notify_record(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::read_reduce(
        ctx.arg->channels[0], output, remoteInput, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event,
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
    ctx.remoteOutput = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], OUTPUT_XN_ID);
    ctx.remoteOutputToken = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], TOKEN_XN_ID);
#if SCENARIO_NUM == 5 || SCENARIO_NUM == 6
    ctx.remoteInput = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], REMOTE_INPUT_XN_ID);
    ctx.remoteInputToken = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], REMOTE_INPUT_TOKEN_XN_ID);
#endif

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
#elif SCENARIO_NUM == 4
    RETURN_IF_CCU_FAIL(ExecuteLoadStore(ctx));
#elif SCENARIO_NUM == 5
    RETURN_IF_CCU_FAIL(ExecuteRead(ctx));
#elif SCENARIO_NUM == 6
    RETURN_IF_CCU_FAIL(ExecuteReadReduce(ctx));
#elif SCENARIO_NUM == 7
    RETURN_IF_CCU_FAIL(ExecuteControlFlowAndLocalNotify(ctx));
#else
#error "SCENARIO_NUM must be 1, 2, 3, 4, 5, 6, or 7"
#endif

    RETURN_IF_CCU_FAIL(ccu::notify_record(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID));
    RETURN_IF_CCU_FAIL(ccu::notify_wait(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID));
    return CCU_SUCCESS;
}

} // namespace ops_hccl_transfer
