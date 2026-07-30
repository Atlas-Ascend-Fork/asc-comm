/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_DATA_UTILS_H
#define ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_DATA_UTILS_H

#include <cstdint>
#include <vector>

namespace ccu_ag_nhr1d_mem2mem {

bool CalculateBufferSizes(
    uint64_t count, uint32_t rankSize, uint64_t& inputBytes,
    uint64_t& outputBytes);

std::vector<float> MakeRankInput(uint32_t rankId, uint64_t count);

bool VerifyAllGatherResult(
    uint32_t rankId, uint32_t rankSize, uint64_t count,
    const std::vector<float>& result);

}  // namespace ccu_ag_nhr1d_mem2mem

#endif
