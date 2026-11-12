/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_TRANSFER_KERNEL_H
#define CCU_TRANSFER_KERNEL_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_primitives.hpp>

namespace ccu = ::AscendC::ccu;

namespace ops_comm_ag {
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

constexpr uint32_t CCU_DIE_NUM = 2;

// 传给 CCU Kernel 的编排上下文：仅保留本 rank 与对端一条链路，及 rank 信息
struct CcuTransferKernelArg {
    ChannelHandle channels[2]{};
    uint32_t channelCount{0};
    uint32_t rankId{0};
    uint32_t rankSize{0};
};

// Kernel 运行期上下文：本端输入/输出地址与 token、数据量，以及对端登记的输出地址与 token

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

// 测试程序的错误检查宏：ACLCHECK / HCCLCHECK 失败时打印文件行号与返回码后提前返回

constexpr uint32_t RANK_SIZE = 2;

// notify 标识与事件掩码：与对端交换输出地址/token、搬运完成、后同步各用一个位
// 通道变量槽位：每条 channel 仅预留 4 个（下标 0-3），槽 1/2 已用于输出地址与 token，
// 对端输入地址与 token 使用空闲的槽 0/3
constexpr uint32_t OUTPUT_XN_ID = 1;

constexpr uint32_t TOKEN_XN_ID = 2;

constexpr uint32_t REMOTE_INPUT_XN_ID = 3;

constexpr uint32_t REMOTE_INPUT_TOKEN_XN_ID = 0;

// CKE[0] 的掩码位 1/2/3/4 已被占用（输出/token 交换、后同步、初始同步），
// 对端输入地址与 token 的交换完成信号使用空闲的 5/6 位
constexpr uint32_t REMOTE_INPUT_XN_MASK_BIT = 5;

constexpr uint32_t REMOTE_INPUT_TOKEN_MASK_BIT = 6;

constexpr uint32_t CKE_IDX_0 = 0;

constexpr uint32_t INIT_SYNC_ID = 4;

constexpr uint32_t POST_SYNC_ID = 3;

constexpr uint16_t EVENT_MASK = 1;

// 把地址与 token 组装成本地/远端地址描述，供 ccu 搬运接口使用
ccu::local_addr MakeLocalAddr(const ccu::variable& address, const ccu::variable& token)
{
    ccu::local_addr addr;
    addr.addr = address;
    addr.token = token;
    return addr;
}

ccu::remote_addr MakeRemoteAddr(const ccu::variable& address, const ccu::variable& token)
{
    ccu::remote_addr addr;
    addr.addr = address;
    addr.token = token;
    return addr;
}

// 与对端互换输出地址与 token，为跨卡写准备目的地址

CcuResult ExchangeRemoteOutput(TransferContext& ctx)
{
    ccu::write_variable_with_notify(ctx.arg->channels[0], ctx.output, OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID);
    ccu::write_variable_with_notify(ctx.arg->channels[0], ctx.outputToken, TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID);
#if SCENARIO_NUM == 5 || SCENARIO_NUM == 6
    // read / read_reduce 场景需要读取对端输入，额外交换对端输入地址
    // （注意槽位受通道预留限制取 0/3，掩码位取空闲的 5/6，与槽位编号无绑定关系）
    ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.input, REMOTE_INPUT_XN_ID, CKE_IDX_0, 1U << REMOTE_INPUT_XN_MASK_BIT);
    ccu::write_variable_with_notify(
        ctx.arg->channels[0], ctx.inputToken, REMOTE_INPUT_TOKEN_XN_ID, CKE_IDX_0, 1U << REMOTE_INPUT_TOKEN_MASK_BIT);
    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID) | (1U << REMOTE_INPUT_XN_MASK_BIT) |
                             (1U << REMOTE_INPUT_TOKEN_MASK_BIT);
#else
    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
#endif
    ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, allBits);
    return CCU_SUCCESS;
}

ccu::func StagedCopyFunc([](ccu::variable srcAddr, ccu::variable srcToken, ccu::variable dstAddr,
                            ccu::variable dstToken, ccu::variable len) {
    ccu::address srcAddrRes;
    srcAddrRes = srcAddr;
    ccu::address dstAddrRes;
    dstAddrRes = dstAddr;

    ccu::local_addr src;
    src.addr = srcAddrRes;
    src.token = srcToken;
    ccu::local_addr dst;
    dst.addr = dstAddrRes;
    dst.token = dstToken;

    ccu::ccu_buffer staging;
    ccu::event done;
    ccu::local_copy(staging, src, len, done, 1);
    ccu::event_wait(done, 1);
    ccu::local_copy(dst, staging, len, done, 1);
    ccu::event_wait(done, 1);
});

// call_func 函数块：封装"经片上缓存中转的两段搬运"这一可复用步骤。
// 5 个 variable 形参全部为入参（源/目的地址与 token、长度），运行期经 in-arg 拷贝流入；
// 函数块内部自建 address/ccu_buffer/event 资源，与调用方解耦。
// 首次调用时合成函数块并录制一次指令，后续调用仅生成跳转，指令空间只占用一份。

// 场景 1：本地拷贝。输入直接写本地输出一次，再经片上 staging buffer 中转写一次，
// 两条路径结果应一致，验证本地搬移的直连与中转两种写法；
// 中转一路改由 call_func 函数块完成，演示带参函数块对真实搬运步骤的封装
CcuResult ExecuteLocalCopy(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    // 中转一路：源/目的地址与 token、长度经 variable 实参传入函数块，块内完成两段搬运
    ccu::call_func<StagedCopyFunc>(ctx.input, ctx.inputToken, ctx.output, ctx.outputToken, ctx.dataSize);
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

    // 循环计数 variable：Ascend 950PR&950DT系列产品的控制流条件仅支持"变量与立即数"比较（双变量比较是
    // A6/V2 能力，A5 上翻译会直接失败），此处与立即数 1 比较，固定循环 1 次
    ccu::variable loopCounter;
    ccu::variable one;
    loopCounter = 0;
    one = 1;

    // local_notify 生产侧：record 与 wait 以相同 tag 配对（同 die 跨 kernel 同步）。
    // 本 kernel 内先 record 后 wait 同一 tag：信号已置位，wait 立即放行，
    // 用于演示配对用法且不阻塞流程
    ccu::event_record("transfer_ready", 0x1);

    // CCU_DO + CCU_WHILE：do-while 循环（body 至少执行一次，条件满足后退出）。
    // 比较立即数须为 uint64_t 字面量
    CCU_DO
    {
        ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
        loopCounter = loopCounter + one;
    }
    CCU_WHILE(loopCounter != 1ULL);

    // CCU_IF + CCU_ELSE：分支选择（恒真走 then 分支，演示 else 存在）
    CCU_IF(loopCounter == 1ULL)
    {
        ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
    }
    CCU_ELSE
    {
        // 不会执行到（loopCounter == 1 恒真），保留以演示 CCU_ELSE 语法
        ccu::local_copy(stagingBuffer, output, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
    }

    ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);

    // local_notify 消费侧：等待与生产侧相同 tag 的信号（先 record 后 wait，立即放行）。
    // 注意 tag 必须与 record 侧完全一致：record 的 tag 若无配对的 wait，
    // kernel 注册（翻译）阶段会因找不到 exported 资源而失败
    ccu::event_wait("transfer_ready", 0x1);
    return CCU_SUCCESS;
}

// 场景 2：跨卡写。输入直接写到对端输出一次，再先落到片上 staging buffer、
// 由 staging buffer 写到对端输出一次，两次都写同一目标，验证跨卡写的直连与中转两种写法

CcuResult ExecuteWrite(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::remote_addr output = MakeRemoteAddr(ctx.remoteOutput, ctx.remoteOutputToken);

    ccu::write(ctx.arg->channels[0], output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    ccu::write(ctx.arg->channels[0], output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return CCU_SUCCESS;
}

// 场景 3：跨卡写并 SUM 规约。先把输入落到本地输出，再与对端做一次同步，
// 最后以求和方式写到对端输出（两 rank 输入相加的结果落在对端）

CcuResult ExecuteWriteReduce(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteOutput = MakeRemoteAddr(ctx.remoteOutput, ctx.remoteOutputToken);

    ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    ccu::notify_record(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID);
    ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID);
    ccu::write_reduce(
        ctx.arg->channels[0], remoteOutput, input, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event,
        EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return CCU_SUCCESS;
}

// 场景 4：load/store + address 演示。
// load 从片上内存地址取 uint64 值装入 variable，store 为其反向操作；
// address 是独立的地址资源类型，支持 imm/var/addr+var 赋值，
// 此处用 address 承载输入地址并装入 local_addr，演示其与 variable 的互换性。

CcuResult ExecuteLoadStore(TransferContext& ctx)
{
    // 1) address 资源：variable 间接赋值后作为 local_addr 的地址成员
    //    （local_addr.addr 为 address 类型，local_addr.token 为 variable 类型）
    ccu::address inputAddr;
    inputAddr = ctx.input; // address = variable（间接寻址地址）

    ccu::local_addr input;
    input.addr = inputAddr;
    input.token = ctx.inputToken; // token 成员为 variable，直接变量赋值

    // 2) variable 间接寻址装载：load(var, var) 与 store(var, var)
    ccu::variable srcAddr;
    srcAddr = ctx.input;
    ccu::variable dstAddr;
    dstAddr = ctx.output;
    ccu::variable loadedByVar;
    ccu::load(srcAddr, loadedByVar);
    ccu::store(dstAddr, loadedByVar);

    // 3) 搬运校验：address 组装的 local_addr 完成完整 input->output 拷贝
    ccu::address outputAddr;
    outputAddr = ctx.output;

    ccu::local_addr output;
    output.addr = outputAddr;
    output.token = ctx.outputToken;

    ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return CCU_SUCCESS;
}

// 场景 5：read 演示（对端写 -> 本端读回，注意 read 参数序为"目的在前，源在后"）。

CcuResult ExecuteRead(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteInput = MakeRemoteAddr(ctx.remoteInput, ctx.remoteInputToken);

    // mem2mem：本端 output <- 对端 remoteInput
    ccu::read(ctx.arg->channels[0], output, remoteInput, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    // buffer：本端 ccu_buffer <- 对端 remoteInput
    ccu::read(ctx.arg->channels[0], stagingBuffer, remoteInput, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    // stagingBuffer 落回 output，验证 buffer 通路读到的数据同样正确
    ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return CCU_SUCCESS;
}

// 场景 6：read_reduce 演示（对端写 -> 本端读取并原地归约）。

CcuResult ExecuteReadReduce(TransferContext& ctx)
{
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);
    const ccu::remote_addr remoteInput = MakeRemoteAddr(ctx.remoteInput, ctx.remoteInputToken);

    // output 先写入初值（对端输入值），read_reduce 后 = 初值 + 对端值（FP32 SUM）
    ccu::local_copy(output, MakeLocalAddr(ctx.input, ctx.inputToken), ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    ccu::notify_record(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID);
    ccu::notify_wait(ctx.arg->channels[0], CKE_IDX_0, 1U << INIT_SYNC_ID);
    ccu::read_reduce(
        ctx.arg->channels[0], output, remoteInput, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event,
        EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排为：读取对端输出 Variable -> 加载任务参数 -> 互换输出地址 -> 按场景搬运 -> 后同步

__ccu_host__ void CcuTransferKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4, uint64_t sqeArgs5, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuTransferKernelArg*>(arg);
    if (kernelArg == nullptr || kernelArg->rankSize != 2U || kernelArg->channelCount != 1U) {
        return;
    }

    TransferContext ctx{};
    ctx.arg = kernelArg;
    // 读取对端通道上登记的输出地址与 token
    ctx.remoteOutput = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], OUTPUT_XN_ID);
    ctx.remoteOutputToken = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], TOKEN_XN_ID);
#if SCENARIO_NUM == 5 || SCENARIO_NUM == 6
    ctx.remoteInput = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], REMOTE_INPUT_XN_ID);
    ctx.remoteInputToken = ccu::get_res_by_channel<ccu::variable>(kernelArg->channels[0], REMOTE_INPUT_TOKEN_XN_ID);
#endif

    // 按 SQE 顺序加载任务参数，load_arg 必须在程序最开始时调用
    uint32_t argId = 0;
    ccu::load_arg(ctx.input, argId++);
    ccu::load_arg(ctx.output, argId++);
    ccu::load_arg(ctx.inputToken, argId++);
    ccu::load_arg(ctx.outputToken, argId++);
    ccu::load_arg(ctx.dataSize, argId++);

    ExchangeRemoteOutput(ctx);

    // 编译期选择搬运场景：1 本地拷贝，2 跨卡写，3 跨卡写并 SUM 规约，4 load/store/address，
    // 5 read，6 read_reduce，7 控制流宏 + local_notify
#if SCENARIO_NUM == 1
    ExecuteLocalCopy(ctx);
#elif SCENARIO_NUM == 2
    ExecuteWrite(ctx);
#elif SCENARIO_NUM == 3
    ExecuteWriteReduce(ctx);
#elif SCENARIO_NUM == 4
    ExecuteLoadStore(ctx);
#elif SCENARIO_NUM == 5
    ExecuteRead(ctx);
#elif SCENARIO_NUM == 6
    ExecuteReadReduce(ctx);
#elif SCENARIO_NUM == 7
    ExecuteControlFlowAndLocalNotify(ctx);
#else
#error "SCENARIO_NUM must be 1, 2, 3, 4, 5, 6, or 7"
#endif

    // 后同步：通知对端本 rank 已完成，并等对端完成
    ccu::notify_record(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID);
    ccu::notify_wait(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID);
}

// 单 rank 线程：建 2 卡通信域与任务流、建链、下发所选场景的传送任务、回读并校验结果

} // namespace ops_comm_ag

#endif // CCU_TRANSFER_KERNEL_H
