/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H
#define CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <hccl/hccl_res.h>
#include <ccu/hcomm/ccu_api_types.h>
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

constexpr uint32_t CCU_MAX_RANK_SIZE = 16;
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr uint32_t CCU_DIE_NUM = 2;

// 本地拷贝的规模常量：ms_interleave 为单个 Loop 的并发分片数，mem_slice 为分片大小 × 单 Loop 分片数得出的单轮搬运量
constexpr uint64_t ccu_ms_interleave = 8;

constexpr uint64_t ccu_ms_size = 4096;

constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;

constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

// Kernel 编排上下文：由 Host 侧下发，携带每条对端 Channel 句柄与 rank 信息
struct CcuKernelArgAllGatherMesh1DMem2Mem {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
    uint32_t rankSize{0};
    uint32_t rankId{0};
};

// 本地拷贝 LoopGroup 的配置：并发分片数、Loop 数、每分片字节数

struct loop_group_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

// 本地拷贝 LoopGroup 的运行期资源：完成事件与中转用 CcuBuffer 数组

struct LoopGroupResource {
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
};

// CalGoSize 的拆分结果：已搬偏移、Loop 循环参数、尾部并行参数、尾部残余字节数

struct GroupOpSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

// 按 Loop 类型缓存的编程实体（执行体、Loop、循环参数），避免每次拷贝重复注册

struct CcuLoopEntity {
    std::unique_ptr<ccu::func> body[2];
    std::unique_ptr<ccu::loop> loops[2];
    ccu::variable loop_param_[2];
};

// Kernel 侧运行期状态：输入/输出/token 变量、卡间偏移、分片大小、切分参数与本地拷贝资源

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    ccu::variable inputSliceStride;
    ccu::variable outputSliceStride;
    ccu::variable chunkSize;
    std::vector<ccu::event> events;

    GroupOpSizeVars goSize;
    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string& loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(const std::string& loopStr) const { return loopMap.count(loopStr) != 0; }
};

// 命令行选项：参与的 Device 列表、每 rank 元素数、输入/输出目录

static constexpr uint64_t SetBitsKernel(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

// 把 Loop 循环参数打包成硬件约定的 64 位格式：高位为上下文 ID，中段为偏移，低位为循环次数

static uint64_t GetLoopParam(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    return ((loop_ctx_id & SetBitsKernel(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & SetBitsKernel(gsa_bit_num)) << gsa_shift_bit) |
           (loop_iter_num_ & SetBitsKernel(loop_num_bit_num));
}

// 把并行参数打包成 64 位格式：重复次数、当前重复轮次、总轮次，供 LoopGroup 按偏移复制多份 Loop 使用

static uint64_t GetParallelParam(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & SetBitsKernel(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & SetBitsKernel(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & SetBitsKernel(total_loop_bit_num)) << total_loop_shift_bit);
}

// 把偏移参数打包成 64 位格式：全局偏移、分片偏移、分片内偏移，供 LoopGroup 定位每份 Loop 的搬移地址

static uint64_t GetOffsetParam(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    return ((gsa_offset & SetBitsKernel(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & SetBitsKernel(ms_bit_num)) << ms_shift_bit) | (cke_offset & SetBitsKernel(cke_bit_num));
}

// 单条 Loop 的最大循环次数上限（Host 侧计算切分时用）

// AllGather 的同步与事件约定：每 16 个 rank 共用一个 event（BITS_PER_EVENT 位），
// 每个 rank 占用其中一位；同一 notify 槽 CKE_IDX_0 上以不同位区分 output 地址、token 写入与后同步完成。
constexpr uint32_t BITS_PER_EVENT = 16;

constexpr int OUTPUT_XN_ID = 1;

constexpr int TOKEN_XN_ID = 2;

constexpr int CKE_IDX_0 = 0;

constexpr int POST_SYNC_ID = 3;

static void InitGroupCopyResources(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr* loopSrc, ccu::local_addr* loopDst, ccu::variable* loopLen)
{
    if (!ctx.resourceAllocated) {
        ctx.moConfig.ms_interleave = ccu_ms_interleave;
        ctx.moConfig.loop_count = CCU_MS_LOCAL_COPY_LOOP_COUNT;
        ctx.moConfig.mem_slice = CCU_LOCAL_COPY_MS_PER_LOOP * ccu_ms_size;

        ctx.moRes.completed_event = ccu::array<ccu::event>(ctx.moConfig.loop_count);
        ctx.moRes.ccuBuf = ccu::array<ccu::ccu_buffer>(ctx.moConfig.loop_count * ctx.moConfig.ms_interleave);
        ctx.resourceAllocated = true;
    }

    const std::string loopType = "localcopy";
    if (!ctx.IsLoopEntityRegistered(loopType)) {
        ctx.CreateLoopEntity(loopType);
        auto& entity = ctx.loopMap[loopType];
        for (uint32_t index = 0; index < 2; index++) {
            uint32_t bufBase = index * ctx.moConfig.ms_interleave;
            ccu::event loopEvt = ctx.moRes.completed_event[index];
            // Loop 执行体：本端内存 -> CcuBuffer -> 本端内存，两段搬运间用 event_wait 确认完成
            entity.body[index].reset(new ccu::func([&ctx, index, bufBase, loopEvt, loopSrc, loopDst, loopLen]() {
                ccu::local_copy(ctx.moRes.ccuBuf[bufBase], loopSrc[index], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
                ccu::local_copy(loopDst[index], ctx.moRes.ccuBuf[bufBase], loopLen[index], loopEvt, 1);
                ccu::event_wait(loopEvt, 1);
            }));
            entity.loops[index].reset(new ccu::loop(entity.loop_param_[index], *entity.body[index]));
        }
    }
}

// 按 CalGoSize 的切分结果组织本地拷贝：addr_offset 非 0 时先整体循环搬运已切分的部分，
// parallel_param 非 0 时再单独搬运尾块（尾分片 + 尾残余），两部分经 CcuBuffer 中转。

static CcuResult GroupCopy(
    AllGatherMesh1DMem2MemContext& ctx, ccu::local_addr dst_, ccu::local_addr src_, GroupOpSizeVars goSize)
{
    ccu::local_addr loopSrc[2];
    ccu::local_addr loopDst[2];
    ccu::variable loopLen[2];

    InitGroupCopyResources(ctx, loopSrc, loopDst, loopLen);
    auto& loops = ctx.loopMap["localcopy"];

    // 已切分的整块：单个 Loop 循环搬运，地址按分片递增
    CCU_IF(goSize.addr_offset != 0)
    {
        ccu::variable loop_param_;
        loop_param_ = GetLoopParam(0, ctx.moConfig.mem_slice * ctx.moConfig.loop_count, 0);
        loop_param_ += goSize.loop_param_;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;

        loopSrc[0].addr = src_.addr;
        loopSrc[0].token = src_.token;
        loopDst[0].addr = dst_.addr;
        loopDst[0].token = dst_.token;
        loopLen[0] = sliceSize;

        loops.loop_param_[0] = loop_param_;
        ccu::variable paraCfg;
        paraCfg = GetParallelParam(ctx.moConfig.loop_count - 1, 0, 1);

        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);
        std::vector<ccu::loop> grpLoops{*loops.loops[0]};
        // 构造 loop_group 即完成提交：LoopGroup 按偏移复制出多份 Loop 并发执行，无独立的提交调用
        ccu::loop_group group(paraCfg, offsetCfg, ctx.moConfig.loop_count, grpLoops);
    }

    // 尾块：loop[0] 搬残余字节，loop[1] 搬一个整分片，二者并行
    CCU_IF(goSize.parallel_param != 0)
    {
        src_.addr += goSize.addr_offset;
        dst_.addr += goSize.addr_offset;

        loopSrc[0].addr = src_.addr;
        loopSrc[0].token = src_.token;
        loopDst[0].addr = dst_.addr;
        loopDst[0].token = dst_.token;
        loopLen[0] = goSize.residual;

        src_.addr += goSize.residual;
        dst_.addr += goSize.residual;

        ccu::variable sliceSize;
        sliceSize = ctx.moConfig.mem_slice;

        loopSrc[1].addr = src_.addr;
        loopSrc[1].token = src_.token;
        loopDst[1].addr = dst_.addr;
        loopDst[1].token = dst_.token;
        loopLen[1] = sliceSize;

        ccu::variable loopCfg0;
        loopCfg0 = GetLoopParam(0, 0, 1);
        ccu::variable loopCfg1;
        loopCfg1 = GetLoopParam(0, 0, 1);
        ccu::variable offsetCfg;
        offsetCfg = GetOffsetParam(ctx.moConfig.mem_slice, ctx.moConfig.ms_interleave, 1);

        loops.loop_param_[0] = loopCfg0;
        loops.loop_param_[1] = loopCfg1;
        std::vector<ccu::loop> grpLoops{*loops.loops[0], *loops.loops[1]};
        // 构造 loop_group 即完成提交：LoopGroup 按偏移复制出多份 Loop 并发执行，无独立的提交调用
        ccu::loop_group group(goSize.parallel_param, offsetCfg, ctx.moConfig.loop_count, grpLoops);
    }

    return CCU_SUCCESS;
}

// Kernel 内资源准备：把各对端 output 地址与 token 绑定到对应 Channel 的 Variable 单元，
// 并按 rank 数分配事件数组

static CcuResult InitResource(AllGatherMesh1DMem2MemContext& ctx)
{
    if (ctx.arg->rankSize < 2 || ctx.arg->rankId >= ctx.arg->rankSize ||
        ctx.arg->channelCount != ctx.arg->rankSize - 1) {
        return CCU_E_PARA;
    }

    ctx.output.resize(ctx.arg->rankSize);
    ctx.token.resize(ctx.arg->rankSize);

    uint32_t channelIdx = 0;
    for (uint32_t peerId = 0; peerId < ctx.arg->rankSize; peerId++) {
        if (peerId != ctx.arg->rankId) {
            ctx.output[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], OUTPUT_XN_ID);
            ctx.token[peerId] = ccu::get_res_by_channel<ccu::variable>(ctx.arg->channels[channelIdx], TOKEN_XN_ID);
            channelIdx++;
        }
    }

    const uint32_t eventNum = (ctx.arg->rankSize + BITS_PER_EVENT - 1) / BITS_PER_EVENT;
    ctx.events.resize(eventNum);
    return CCU_SUCCESS;
}

// 前同步：本端 output 地址与 token 写入对端并置位对端 notify，
// 再等待对端把其地址与 token 写回本端，避免读到旧值

static void PreSync(AllGatherMesh1DMem2MemContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.output[ctx.arg->rankId], OUTPUT_XN_ID, CKE_IDX_0, 1U << OUTPUT_XN_ID));
        CCU_CHK_VOID_RET(ccu::write_variable_with_notify(
            ctx.arg->channels[i], ctx.token[ctx.arg->rankId], TOKEN_XN_ID, CKE_IDX_0, 1U << TOKEN_XN_ID));
    }

    // 等对端把 output 地址与 token 都写回本端后才可组装远端地址
    const uint32_t allBits = (1U << OUTPUT_XN_ID) | (1U << TOKEN_XN_ID);
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, allBits));
    }
}

// 后同步：告知所有对端本 rank 搬运完成，并等所有对端完成，AllGather 才算结束

static void PostSync(AllGatherMesh1DMem2MemContext& ctx)
{
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_record(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
    for (uint32_t i = 0; i < ctx.arg->channelCount; i++) {
        CCU_CHK_VOID_RET(ccu::notify_wait(ctx.arg->channels[i], CKE_IDX_0, 1U << POST_SYNC_ID));
    }
}

// 提交跨 rank 写任务：把本 rank 分片异步写入其余 rank，mask 位标记搬运完成后置位的 event 标志；
// 本 rank 位用 event_record 补齐（本地拷贝分支不会自动置位）

static CcuResult DoAllGatherWrite(
    AllGatherMesh1DMem2MemContext& ctx, const ccu::local_addr& src_, const std::vector<ccu::remote_addr>& dst_)
{
    uint32_t channelIdx = 0;
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        const uint32_t eventIdx = rankIdx / BITS_PER_EVENT;
        const uint16_t rankMask = 1U << (rankIdx % BITS_PER_EVENT);
        if (rankIdx == ctx.arg->rankId) {
            CCU_CHK_RET(ccu::event_record(ctx.events[eventIdx], rankMask));
            continue;
        }
        CCU_CHK_RET(ccu::write(
            ctx.arg->channels[channelIdx], dst_[rankIdx], src_, ctx.chunkSize, ctx.events[eventIdx], rankMask));
        channelIdx++;
    }
    return CCU_SUCCESS;
}

// 等待所有搬运分支完成：逐个 event 等待其覆盖的 rank 位全部置位

static CcuResult DoAllGatherWait(AllGatherMesh1DMem2MemContext& ctx)
{
    const uint32_t eventNum = static_cast<uint32_t>(ctx.events.size());
    for (uint32_t eventIdx = 0; eventIdx < eventNum; eventIdx++) {
        const uint32_t remainingRanks = ctx.arg->rankSize - eventIdx * BITS_PER_EVENT;
        const uint32_t bitCount = std::min<uint32_t>(remainingRanks, BITS_PER_EVENT);
        const uint16_t eventMask = bitCount == BITS_PER_EVENT ? static_cast<uint16_t>(UINT16_MAX) :
                                                                static_cast<uint16_t>((1U << bitCount) - 1);
        CCU_CHK_RET(ccu::event_wait(ctx.events[eventIdx], eventMask));
    }
    return CCU_SUCCESS;
}

// AllGather 执行：组装源/目的地址后，先向其余 rank 写本 rank 分片，再做本 rank 分片的本地拷贝，最后统一等待

static CcuResult DoAllGather(AllGatherMesh1DMem2MemContext& ctx)
{
    // 源地址：本 rank 输入缓冲中偏移 rankId 个 stride 的位置（本 rank 要发送的分片）
    ccu::local_addr src_;
    src_.addr = ctx.input;
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        src_.addr += ctx.inputSliceStride;
    }
    src_.token = ctx.token[ctx.arg->rankId];

    // 本地目的地址：本 rank 输出缓冲中属于本 rank 的那一段，直接取本端变量
    ccu::local_addr localDst;
    localDst.addr = ctx.output[ctx.arg->rankId];
    for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
        localDst.addr += ctx.outputSliceStride;
    }
    localDst.token = ctx.token[ctx.arg->rankId];

    // 远端目的地址：由前同步换来的对端 output 地址、token 加同一卡间偏移构成
    std::vector<ccu::remote_addr> dst_(ctx.arg->rankSize);
    for (uint32_t rankIdx = 0; rankIdx < ctx.arg->rankSize; rankIdx++) {
        if (rankIdx == ctx.arg->rankId) {
            continue;
        }
        dst_[rankIdx].addr = ctx.output[rankIdx];
        for (uint32_t i = 0; i < ctx.arg->rankId; i++) {
            dst_[rankIdx].addr += ctx.outputSliceStride;
        }
        dst_[rankIdx].token = ctx.token[rankIdx];
    }

    CCU_CHK_RET(DoAllGatherWrite(ctx, src_, dst_));
    CCU_CHK_RET(GroupCopy(ctx, localDst, src_, ctx.goSize));
    CCU_CHK_RET(DoAllGatherWait(ctx));
    return CCU_SUCCESS;
}

// Host 侧 CCU Kernel：通过 SQE 下发 CCU 编排指令。
// 编排三阶段：前同步互换地址 -> 执行 AllGather -> 后同步确认完成。

__ccu_host__ void CcuAllGatherMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4, uint64_t sqeArgs5, uint64_t sqeArgs6,
    uint64_t sqeArgs7, uint64_t sqeArgs8, uint64_t sqeArgs9, uint64_t sqeArgs10, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuKernelArgAllGatherMesh1DMem2Mem*>(arg);
    AllGatherMesh1DMem2MemContext ctx;
    ctx.arg = kernelArg;

    // 绑定各对端 Variable 单元并分配事件数组
    CCU_CHK_VOID_RET(InitResource(ctx));

    // 按 SQE 声明顺序加载任务参数，load_arg 必须在程序最开始时调用
    uint32_t argId = 0;
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.input, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.output[ctx.arg->rankId], argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.token[ctx.arg->rankId], argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.inputSliceStride, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.outputSliceStride, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.chunkSize, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.addr_offset, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.loop_param_, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.parallel_param, argId++));
    CCU_CHK_VOID_RET(ccu::load_arg(ctx.goSize.residual, argId++));

    PreSync(ctx);
    CCU_IF(ctx.chunkSize != 0) { CCU_CHK_VOID_RET(DoAllGather(ctx)); }
    PostSync(ctx);
}

// 通信入口：完成资源申请与参数准备后，通过 <<<>>> 把 AllGather 任务直调下发到 Stream

} // namespace ops_comm_ag

#endif // CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H
