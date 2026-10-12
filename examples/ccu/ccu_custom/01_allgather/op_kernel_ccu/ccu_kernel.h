/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H
#define HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ccu/hcomm/ccu_array.hpp>
#include <ccu/hcomm/ccu_buffer.hpp>
#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_func.hpp>
#include <ccu/hcomm/ccu_loop.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <ccu/hcomm/ccu_api_types.h>
#include <ccu/hcomm/ccu_variable.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_ag {

#define RETURN_IF_CCU_FAIL(call)  \
    do {                          \
        CcuResult ret = (call);   \
        if (ret != CCU_SUCCESS) { \
            return ret;           \
        }                         \
    } while (0)

constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint64_t ccu_ms_interleave = 8;
constexpr uint64_t ccu_ms_size = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

struct loop_group_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

struct GroupOpSize {
    uint64_t addr_offset;
    uint64_t loop_param_;
    uint64_t parallel_param;
    uint64_t residual;
};

constexpr uint64_t set_bits(uint16_t start, uint16_t end)
{
    return ((uint64_t(1) << (end - start + 1)) - uint64_t(1)) << start;
}

constexpr uint64_t set_bits(uint16_t end) { return (uint64_t(1) << (end + 1)) - uint64_t(1); }

constexpr uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loop_num_bit_num = 12;
    return set_bits(loop_num_bit_num);
}

constexpr uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num_)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    constexpr uint16_t loop_num_shift_bit = 0;
    return ((loop_ctx_id & set_bits(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((loop_iter_num_ & set_bits(loop_num_bit_num)) << loop_num_shift_bit);
}

constexpr uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & set_bits(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & set_bits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & set_bits(total_loop_bit_num)) << total_loop_shift_bit);
}

constexpr uint64_t get_offset_param(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    constexpr uint16_t cke_shift_bit = 0;
    return ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & set_bits(ms_bit_num)) << ms_shift_bit) |
           ((cke_offset & set_bits(cke_bit_num)) << cke_shift_bit);
}

inline GroupOpSize CalGoSize(uint64_t size, const loop_group_config& config)
{
    uint64_t loopSize = config.loop_count * config.mem_slice;
    uint64_t maxSize = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.mem_slice;
    uint64_t p = size - m * loopSize - n * config.mem_slice;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loop_count - 1;
        p = config.mem_slice;
    }

    uint64_t loopExtendNum = 0;
    uint64_t tailSize = 0;
    constexpr uint64_t loopNumTwo = 2;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = get_parallel_param(n - 1, 0, 1);
        tailSize = config.mem_slice;
    } else if (n == 0 && p != 0) {
        loopExtendNum = get_parallel_param(0, 0, 1);
        tailSize = p;
    } else {
        loopExtendNum = get_parallel_param(n - 1, 1, loopNumTwo);
        tailSize = p;
    }

    return {config.mem_slice * config.loop_count * m, m, loopExtendNum, tailSize};
}

struct LoopGroupResource {
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
    uint32_t eventCount;
    uint32_t bufCount;
};

struct GroupOpSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

struct CcuLoopEntity {
    std::unique_ptr<ccu::func> body[2];
    std::unique_ptr<ccu::loop> loops[2];
    ccu::variable loop_param_[2];
};

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t channelCount;
};

struct ccu_kernel_info {
    char kernel_func_name[64];
    void* kernel_func;
    void* kernel_arg;

private:
    std::shared_ptr<CcuKernelArgBase> kernelArgSmartPtr;

public:
    template <typename t>
    void setKernelArg(std::shared_ptr<t> arg)
    {
        kernelArgSmartPtr = std::static_pointer_cast<CcuKernelArgBase>(arg);
        kernel_arg = static_cast<void*>(arg.get());
    }
};

struct CcuKernelArgAllGatherMesh1DMem2Mem : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
};

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    ccu::variable currentRankSliceInputOffset;
    ccu::variable currentRankSliceOutputOffset;
    ccu::variable sliceSize;
    ccu::event event;

    GroupOpSizeVars goSize;
    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string& loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(const std::string& loopStr) { return loopMap.count(loopStr) != 0; }
};

CcuResult CcuAllGatherMesh1DMem2MemKernel(ccu_kernel_arg arg);

} // namespace ops_hccl_ag

#endif // HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H
