/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_DATA_UTILS_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_DATA_UTILS_H

#include <cstdint>
#include <vector>

namespace ccu_ag_mem2mem {

bool CalculateBufferSizes(
    uint64_t count, uint32_t rankSize, uint64_t& inputBytes,
    uint64_t& outputBytes);

std::vector<float> MakeRankInput(uint32_t rankId, uint64_t count);

bool VerifyAllGatherResult(
    uint32_t rankId, uint32_t rankSize, uint64_t count,
    const std::vector<float>& result);

}  // namespace ccu_ag_mem2mem

#endif  // ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_DATA_UTILS_H
