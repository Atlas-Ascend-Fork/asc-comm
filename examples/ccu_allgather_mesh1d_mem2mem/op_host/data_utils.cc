/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 */
#include "data_utils.h"

#include <algorithm>
#include <iostream>
#include <limits>

namespace ccu_ag_mem2mem {

bool CalculateBufferSizes(
    uint64_t count, uint32_t rankSize, uint64_t& inputBytes,
    uint64_t& outputBytes)
{
    inputBytes = 0;
    outputBytes = 0;
    if (rankSize == 0 ||
        count > std::numeric_limits<uint64_t>::max() / sizeof(float)) {
        return false;
    }
    inputBytes = count * sizeof(float);
    if (inputBytes != 0 &&
        rankSize > std::numeric_limits<uint64_t>::max() / inputBytes) {
        inputBytes = 0;
        return false;
    }
    outputBytes = inputBytes * rankSize;
    return outputBytes <= std::numeric_limits<size_t>::max();
}

std::vector<float> MakeRankInput(uint32_t rankId, uint64_t count)
{
    std::vector<float> input(std::max<uint64_t>(count, 1));
    for (uint64_t index = 0; index < count; ++index) {
        input[index] = static_cast<float>(rankId * 1000000ULL + index + 1);
    }
    return input;
}

bool VerifyAllGatherResult(
    uint32_t rankId, uint32_t rankSize, uint64_t count,
    const std::vector<float>& result)
{
    if (result.size() != count * rankSize) {
        std::cerr << "rank " << rankId << " result size mismatch\n";
        return false;
    }
    for (uint32_t sourceRank = 0; sourceRank < rankSize; ++sourceRank) {
        for (uint64_t indexInRank = 0; indexInRank < count; ++indexInRank) {
            const float expected =
                static_cast<float>(sourceRank * 1000000ULL + indexInRank + 1);
            const uint64_t index = sourceRank * count + indexInRank;
            if (result[index] != expected) {
                std::cerr << "rank " << rankId << " mismatch at " << index
                          << ": expected " << expected << ", got "
                          << result[index] << "\n";
                return false;
            }
        }
    }
    return true;
}

}  // namespace ccu_ag_mem2mem
