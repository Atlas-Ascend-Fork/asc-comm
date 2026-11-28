/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef MC2_BENCH_WRITE_CASE_VALIDATION_H
#define MC2_BENCH_WRITE_CASE_VALIDATION_H
#include <cstdint>
#include <limits>

namespace asccomm_api_bench_device {
// Bound byte offsets before kernel compilation, avoiding device 128-bit overflow helpers.
inline bool ValidWriteCase(uint64_t count, int64_t ranks, uint64_t width)
{
    if (ranks <= 0 || ranks > 384 || count == 0 || (width != 1 && width != 2 && width != 4 && width != 8) ||
        count % static_cast<uint64_t>(ranks) != 0 || count > std::numeric_limits<uint64_t>::max() / width)
        return false;
    const uint64_t peer = count / static_cast<uint64_t>(ranks) * width;
    if (peer > std::numeric_limits<uint64_t>::max() - 511)
        return false;
    const uint64_t stride = (peer + 511) / 512 * 512;
    return stride <= std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(ranks);
}
} // namespace asccomm_api_bench_device
#endif
