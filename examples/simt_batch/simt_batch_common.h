/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_BATCH_COMMON_H
#define EXAMPLES_SIMT_BATCH_COMMON_H

#include <cstddef>
#include <cstdint>

namespace simt_batch {

constexpr uint32_t kRootRank = 0U;
constexpr uint32_t kPayloadBytes = 64U;
constexpr size_t kBufferBytes = kPayloadBytes;
constexpr uint8_t kPayloadBase = 0x10U;

} // namespace simt_batch

#endif // EXAMPLES_SIMT_BATCH_COMMON_H
