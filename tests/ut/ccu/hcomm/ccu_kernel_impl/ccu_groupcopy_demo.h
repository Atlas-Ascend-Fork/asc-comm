/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_GROUPCOPY_DEMO_H
#define CCU_GROUPCOPY_DEMO_H

#include "ccu/hcomm/ccu_primitives.hpp"
#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/common/ccu_log.h" // demo演示使用，hccl仓需要另外实现

#include <cstdint>
#include <climits>
#include <memory>
#include <vector>
#include <string>

namespace ccu = ::AscendC::ccu;

constexpr uint32_t AG_MAX_RANK_SIZE = 16;

constexpr int AG_OUTPUT_XN_ID = 1;
constexpr int AG_TOKEN_XN_ID = 2;
constexpr int AG_CKE_IDX_0 = 0;
constexpr int AG_POST_SYNC_ID = 3;

constexpr uint64_t AG_CCU_MS_SIZE = 4096;
constexpr uint64_t AG_CCU_MS_INTERLEAVE = 8;
constexpr uint64_t AG_CCU_MS_DEFAULT_LOOP_COUNT = 64;

constexpr uint64_t AG_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint64_t AG_CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

// =============================================================================
// 参数结构体
// =============================================================================

struct AllGatherKernelArg {
    uint64_t rankSize;
    uint32_t rankId;
    ChannelHandle channels[AG_MAX_RANK_SIZE];
    uint32_t channelCount;
};

struct GroupCopyGoSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

// =============================================================================
// 运行时配置
// =============================================================================

struct GroupCopyLoopGroupConfig {
    uint64_t ms_interleave;
    uint64_t loop_count;
    uint64_t mem_slice;
};

struct GroupCopyLoopGroupResource {
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
    uint32_t eventCount{0};
    uint32_t bufCount{0};
};

// =============================================================================
// Bit-packing 辅助（纯 host 计算，无 IR 副作用）
// =============================================================================

static inline constexpr uint64_t AgSetBits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static inline uint64_t AgGetMaxLoopIterNum()
{
    constexpr uint16_t loop_num_bit_num = 12;
    return AgSetBits(loop_num_bit_num);
}

static inline uint64_t AgGetLoopParam(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    constexpr uint16_t loop_num_shift_bit = 0;
    return ((loop_ctx_id & AgSetBits(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & AgSetBits(gsa_bit_num)) << gsa_shift_bit) |
           ((loop_iter_num_ & AgSetBits(loop_num_bit_num)) << loop_num_shift_bit);
}

static inline uint64_t AgGetParallelParam(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & AgSetBits(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & AgSetBits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & AgSetBits(total_loop_bit_num)) << total_loop_shift_bit);
}

static inline uint64_t AgGetOffsetParam(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    constexpr uint16_t cke_shift_bit = 0;
    return ((gsa_offset & AgSetBits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & AgSetBits(ms_bit_num)) << ms_shift_bit) |
           ((cke_offset & AgSetBits(cke_bit_num)) << cke_shift_bit);
}

// =============================================================================
// Kernel 上下文（纯 POD，没有任何带 IR 副作用的代码）
// =============================================================================

struct AllGatherContext {
    const AllGatherKernelArg* arg;

    ccu::variable input;
    ccu::variable output[AG_MAX_RANK_SIZE];
    ccu::variable token[AG_MAX_RANK_SIZE];

    ccu::variable currentRankSliceInputOffset;
    ccu::variable currentRankSliceOutputOffset;
    ccu::variable tmpRepeatNum;
    ccu::variable inputRepeatStride;
    ccu::variable outputRepeatStride;
    ccu::variable normalSliceSize;
    ccu::variable lastSliceSize;
    ccu::variable isInputOutputEqual;

    GroupCopyGoSizeVars goSize;

    ccu::event event;
    ccu::local_addr srcLocCopy;
    ccu::local_addr localDst;

    GroupCopyLoopGroupConfig moConfig;
    GroupCopyLoopGroupResource moRes;
    bool resourceAllocated;
    bool groupCopyRegistered;

    std::unique_ptr<ccu::func> copyBody[2];
    std::unique_ptr<ccu::loop> copyLoops[2];
    ccu::variable copyLoopParam[2];

    ccu::local_addr loopSrc[2];
    ccu::local_addr loopDst[2];
    ccu::variable loopLen[2];
};

// =============================================================================
// AllocGoResource —— 纯资源分配，不展开 CCU_IF，放在头里 inline 安全
// =============================================================================

static inline CcuResult AgAllocGoResource(
    GroupCopyLoopGroupConfig& config, GroupCopyLoopGroupResource& res, bool& allocated,
    uint32_t parallelDim = AG_CCU_MS_DEFAULT_LOOP_COUNT, uint32_t msPerLoop = 1)
{
    if (allocated) {
        return CCU_SUCCESS;
    }

    config.ms_interleave = AG_CCU_MS_INTERLEAVE;
    config.loop_count = parallelDim;
    config.mem_slice = msPerLoop * AG_CCU_MS_SIZE;

    res.eventCount = config.loop_count;
    res.completed_event = ccu::array<ccu::event>(res.eventCount);

    res.bufCount = config.loop_count * config.ms_interleave;
    res.ccuBuf = ccu::array<ccu::ccu_buffer>(res.bufCount);

    allocated = true;
    return CCU_SUCCESS;
}

CcuResult AgGroupCopy(AllGatherContext& ctx, ccu::local_addr dst_, ccu::local_addr src_, GroupCopyGoSizeVars& goSize);

CcuResult CcuAllGatherMesh1dMem2MemKernel(ccu_kernel_arg arg);

#endif // CCU_GROUPCOPY_DEMO_H
