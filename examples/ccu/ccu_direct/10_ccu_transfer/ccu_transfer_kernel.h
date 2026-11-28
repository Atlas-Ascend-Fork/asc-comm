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
#include <ccu/ccu_api/ccu_types.h>
#include <ccu/ccu_api/ccu_primitives.h>

namespace ccu = ::asc::ccu;

namespace ops_comm_ag {
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

constexpr uint32_t CCU_DIE_NUM = 2;

// 传给 CCU Kernel 的编排上下文：仅保留本 rank 与对端一条链路，及 rank 信息
struct CcuTransferKernelArg {
    ChannelHandle channels[2]{};
    uint32_t channelCount{0};
    uint32_t rankId{0};
    uint32_t rankSize{0};
    // 立即数地址演示用：taskArgs 翻译期不可见（load_arg 存在的原因），
    // 立即数地址形态须经 kernelArg 传入、翻译期固化进指令
    uint64_t immInputAddr{0};
    uint64_t immOutputAddr{0};
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

ccu_result ExchangeRemoteOutput(TransferContext& ctx)
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
    return ccu_result::CCU_SUCCESS;
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
ccu_result ExecuteLocalCopy(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    // 中转一路：源/目的地址与 token、长度经 variable 实参传入函数块，块内完成两段搬运
    ccu::call_func<StagedCopyFunc>(ctx.input, ctx.inputToken, ctx.output, ctx.outputToken, ctx.dataSize);
    return ccu_result::CCU_SUCCESS;
}

// 场景 7：CCU_DO/CCU_WHILE/CCU_IF/CCU_ELSE 软件控制流宏 + local_notify 跨 kernel 同步。
// 以"路径位累加器"确保每个控制宏的预期路径都实际执行：
//   - do-while 循环 1 遍，result += 0b0001；
//   - 独立 CCU_WHILE 循环 1 遍（前判语义），result += 0b0010；
//   - 独立 CCU_IF 走 true 分支，result += 0b0100；
//   - CCU_IF + CCU_ELSE 走 false（else）分支，result += 0b1000；
// 四条路径全部走对后 result == 0b1111，终检门控据此选择最终搬运通路，回读校验才 PASS。
// 各宏的条件与前序宏的执行结果关联（如 IF 的条件引用 while 的循环计数），
// 任一路径未按预期执行都会使 result 缺位，被终检暴露。
// event_record("tag", mask)/event_wait("tag", mask) 为 local_notify 重载（同 die 跨 kernel）。

ccu_result ExecuteControlFlowAndLocalNotify(TransferContext& ctx)
{
    ccu::ccu_buffer stagingBuffer;

    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    // 控制流条件仅支持"变量与立即数"比较（双变量比较是 V2 能力，V1 上翻译会直接失败），
    // 立即数不可直接参与算术，故位值与计数均先赋给 variable
    ccu::variable result;
    result = 0;
    ccu::variable bitDoWhile;
    bitDoWhile = 0b0001;
    ccu::variable bitWhile;
    bitWhile = 0b0010;
    ccu::variable bitIf;
    bitIf = 0b0100;
    ccu::variable bitIfElse;
    bitIfElse = 0b1000;
    ccu::variable one;
    one = 1;
    ccu::variable doCounter;
    doCounter = 0;
    ccu::variable whileCounter;
    whileCounter = 0;

    // local_notify 生产侧：record 与 wait 以相同 tag 配对（同 die 跨 kernel 同步）。
    // 本 kernel 内先 record 后 wait 同一 tag：信号已置位，wait 立即放行，
    // 用于演示配对用法且不阻塞流程
    ccu::event_record("transfer_ready", 0x1);

    // 1) CCU_DO + CCU_WHILE：do-while 循环 1 遍（body 至少执行一次，条件满足后退出），
    //    置 0b0001；body 内含本端片上内存 -> 本端CCU Buffer 搬运
    CCU_DO
    {
        ccu::local_copy(stagingBuffer, input, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
        result = result + bitDoWhile;
        doCounter = doCounter + one;
    }
    CCU_WHILE(doCounter != 1ULL);

    // 2) 独立 CCU_WHILE 循环：先判断条件再执行 body（与 do-while 的"先执行后判断"相对），
    //    循环 1 遍，置 0b0010
    CCU_WHILE(whileCounter != 1ULL)
    {
        result = result + bitWhile;
        whileCounter = whileCounter + one;
    }

    // 3) 独立 CCU_IF：走 true 分支，置 0b0100。
    //    条件关联 while 的执行结果：while 恰好循环 1 遍时 whileCounter == 1 为真
    CCU_IF(whileCounter == 1ULL) { result = result + bitIf; }

    // 4) CCU_IF + CCU_ELSE：走 false（else）分支，置 0b1000。
    //    前三步全部按预期执行时 result == 0b0111，条件为假进入 else
    CCU_IF(result != 0b0111ULL)
    {
        // 前序宏未按预期执行才会进入此分支：不置位，交由终检暴露
    }
    CCU_ELSE { result = result + bitIfElse; }

    // 5) 终检门控：四个控制宏全部按预期路径执行后 result == 0b1111
    CCU_IF(result == 0b1111ULL)
    {
        // 全部路径走对：staging 落回 output（本端CCU Buffer -> 本端片上内存），
        // 输出 = 输入，回读校验 PASS
        ccu::local_copy(output, stagingBuffer, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
    }
    CCU_ELSE
    {
        // 任一路径未走对：把 result 写进 output 首 8 字节，写坏数据使回读校验 FAIL
        ccu::variable dstAddr;
        dstAddr = ctx.output;
        ccu::store(dstAddr, result);
    }

    // local_notify 消费侧：等待与生产侧相同 tag 的信号（先 record 后 wait，立即放行）。
    // 注意 tag 必须与 record 侧完全一致：record 的 tag 若无配对的 wait，
    // kernel 注册（翻译）阶段会因找不到 exported 资源而失败
    ccu::event_wait("transfer_ready", 0x1);
    return ccu_result::CCU_SUCCESS;
}

// 场景 2：跨卡写。输入直接写到对端输出一次，再先落到片上 staging buffer、
// 由 staging buffer 写到对端输出一次，两次都写同一目标，验证跨卡写的直连与中转两种写法

ccu_result ExecuteWrite(TransferContext& ctx)
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
    return ccu_result::CCU_SUCCESS;
}

// 场景 3：跨卡写并 SUM 规约。先把输入落到本地输出，再与对端做一次同步，
// 最后以求和方式写到对端输出（两 rank 输入相加的结果落在对端）

ccu_result ExecuteWriteReduce(TransferContext& ctx)
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
    return ccu_result::CCU_SUCCESS;
}

// 场景 4：load/store + address 演示。
// load 从片上内存地址取 uint64 值装入 variable，store 为其反向操作；
// 地址源覆盖 variable 间接寻址与立即数两类，立即数形态翻译期固化；
// address 是独立的地址资源类型，支持 imm/var/addr 间赋值与 addr+addr / addr+var / var+addr
// 三种加法写法，本场景用加法链求得输入地址并完成搬运校验。

ccu_result ExecuteLoadStore(TransferContext& ctx)
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

    // 3) 立即数地址装载：load(imm, var) 与 store(imm, var)。
    //    taskArgs 翻译期不可见（load_arg 存在的原因），立即数地址经 kernelArg 传入，
    //    翻译期固化进指令；token 由翻译期统一的 HBM 内存 token 提供
    ccu::variable loadedByImm;
    ccu::load(ctx.arg->immInputAddr, loadedByImm);
    ccu::store(ctx.arg->immOutputAddr, loadedByImm);

    // 4) address 立即数赋值与加法：立即数不可直接参与算术，零偏移先赋给 variable/address，
    //    三种加法写法与就地加求和后地址仍指向输入缓冲，供第 6) 步搬运校验
    ccu::address immAddr;
    immAddr = ctx.arg->immInputAddr; // address = 立即数（uint64_t）

    ccu::variable zeroVar;
    zeroVar = 0;
    ccu::address zeroAddr;
    zeroAddr = 0; // address = 立即数 0

    ccu::address sumByVar;
    sumByVar = immAddr + zeroVar; // address = address + variable
    ccu::address sumByVarSwap;
    sumByVarSwap = zeroVar + immAddr; // address = variable + address（交换律写法）
    ccu::address sumByAddr;
    sumByAddr = immAddr + zeroAddr; // address = address + address
    sumByAddr += zeroAddr;          // address += address（就地加，值不变）

    // 5) 搬运校验 1：variable 路径 address 组装的 local_addr 完成完整 input->output 拷贝
    ccu::address outputAddr;
    outputAddr = ctx.output;

    ccu::local_addr output;
    output.addr = outputAddr;
    output.token = ctx.outputToken;

    ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);

    // 6) 搬运校验 2：立即数 + 加法链求得的地址同样完成 input->output 拷贝，
    //    验证 imm/arith 地址来源与 variable 路径结果一致
    ccu::local_addr inputBySum;
    inputBySum.addr = sumByAddr;
    inputBySum.token = ctx.inputToken;

    ccu::local_copy(output, inputBySum, ctx.dataSize, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);

    // 7) 数组批量装载：load/store 的 array<variable> 形态（元素须物理连续、num ≤ 数组长度），
    //    立即数与 variable 间接寻址各覆盖读/写通路，按 8 字节步进连续搬运整块数据；
    //    bulkVars 从 input 装入后写回 output 且置于最后一步，最终回读值即批量通路的结果
    ccu::array<ccu::variable> bulkVars(2);
    ccu::load(ctx.arg->immInputAddr, bulkVars, 2);   // 立即数地址 -> variable 数组
    ccu::store(ctx.arg->immOutputAddr, bulkVars, 2); // variable 数组 -> 立即数地址
    ccu::load(srcAddr, bulkVars, 2);                 // variable 地址 -> variable 数组
    ccu::store(dstAddr, bulkVars, 2);                // variable 数组 -> variable 地址
    return ccu_result::CCU_SUCCESS;
}

// 场景 5：read 演示（对端写 -> 本端读回，注意 read 参数序为"目的在前，源在后"）。

ccu_result ExecuteRead(TransferContext& ctx)
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
    return ccu_result::CCU_SUCCESS;
}

// 场景 6：read_reduce 演示（对端写 -> 本端读取并原地归约）。

ccu_result ExecuteReadReduce(TransferContext& ctx)
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
    return ccu_result::CCU_SUCCESS;
}

// 场景 8：片上内存原地归约 local_reduce 演示（本端片上内存 -> 本端片上内存）。
// 归约为原地操作，dst 须已写入有效初值：output 由 host 侧清零，初值即 0；
// 先做 SUM 再做 MAX，结果 = max(0 + 输入值, 输入值) = 输入值，回读校验保持一致

ccu_result ExecuteLocalReduce(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    ccu::local_reduce(output, input, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    ccu::local_reduce(output, input, ctx.dataSize, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_MAX, ctx.event, EVENT_MASK);
    ccu::event_wait(ctx.event, EVENT_MASK);
    return ccu_result::CCU_SUCCESS;
}

// 场景 9：config-based 硬件 Loop 演示。
// loop/loop_group 的编译期配置形态：迭代参数翻译期固化为常量，
// 与 var-based 形态（由 variable 位段在执行期决定）相对。
// 缓冲区仅 4 个 FP32（16 字节），故地址偏移置 0、固定 2 轮，两轮重复拷贝同一块，
// 结果仍为 input；LoopGroup 为单 Loop、无克隆，构造即完成提交

ccu_result ExecuteConfigLoop(TransferContext& ctx)
{
    const ccu::local_addr input = MakeLocalAddr(ctx.input, ctx.inputToken);
    const ccu::local_addr output = MakeLocalAddr(ctx.output, ctx.outputToken);

    // 循环体：无入参 lambda，仅含本地搬运与等待
    //（loop body 内禁止 event_record/notify 类接口与软件分支宏）
    ccu::func body([&input, &output, &ctx]() {
        ccu::local_copy(output, input, ctx.dataSize, ctx.event, EVENT_MASK);
        ccu::event_wait(ctx.event, EVENT_MASK);
    });

    // config-based loop：迭代参数翻译期固化
    ccu_loop_config loopCfg{};
    loopCfg.addr_offset = 0;
    loopCfg.iter_num = 2;
    ccu::loop transferLoop(loopCfg, body);

    // config-based loop_group：单 Loop 场景全字段置 0，构造即完成提交
    ccu_loop_group_config groupCfg{};
    ccu::loop_group group(groupCfg, 1, {transferLoop});
    return ccu_result::CCU_SUCCESS;
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
    // 5 read，6 read_reduce，7 控制流宏 + local_notify，8 片上内存原地归约，9 config-based 硬件Loop
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
#elif SCENARIO_NUM == 8
    ExecuteLocalReduce(ctx);
#elif SCENARIO_NUM == 9
    ExecuteConfigLoop(ctx);
#else
#error "SCENARIO_NUM must be 1, 2, 3, 4, 5, 6, 7, 8, or 9"
#endif

    // 后同步：通知对端本 rank 已完成，并等对端完成
    ccu::notify_record(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID);
    ccu::notify_wait(kernelArg->channels[0], CKE_IDX_0, 1U << POST_SYNC_ID);
}

// 单 rank 线程：建 2 卡通信域与任务流、建链、下发所选场景的传送任务、回读并校验结果

} // namespace ops_comm_ag

#endif // CCU_TRANSFER_KERNEL_H
