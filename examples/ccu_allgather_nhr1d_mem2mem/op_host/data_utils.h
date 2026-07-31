/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_NHR1D_MEM2MEM_DATA_UTILS_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_NHR1D_MEM2MEM_DATA_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace CcuAgNhr1dMem2mem {

bool CalculateBufferSizes(
    uint64_t count, uint32_t rankSize, uint64_t& inputBytes,
    uint64_t& outputBytes);

bool ReadFloatData(
    const std::string& filePath, uint64_t count, std::vector<float>& data);

bool WriteFloatData(
    const std::string& filePath, const std::vector<float>& data);

}  // namespace CcuAgNhr1dMem2mem

#endif  // ASC_COMM_EXAMPLE_CCU_ALLGATHER_NHR1D_MEM2MEM_DATA_UTILS_H
