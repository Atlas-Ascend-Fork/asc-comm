/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_PRINT_INTERNAL_H
#define ASCCOMM_CCU_PRINT_INTERNAL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace AscendC {
namespace ccu {
namespace dfx {
namespace detail {

constexpr uint64_t PRINT_RING_DEPTH = 32;

// Fixed wire header followed by PRINT_RING_DEPTH records. Cursors are byte
// offsets into the payload, not addresses. The CCU owns tail; the Host owns head.
// On every CCU version, head is encoded as uint64_t{0} - byteOffset.
// Modular addition then tests equality using an immediate-zero branch, without
// requiring variable-to-variable comparisons (unsupported by A5).
// A trailing allocation replaces the old non-standard C++ flexible array.
struct PrintRingHeader {
    uint64_t tail;
    uint64_t head;
    uint64_t dropped;
};
static_assert(sizeof(PrintRingHeader) == 24, "CCU print wire header must stay 24 bytes");

bool ValidateFormat(const char* fmt, size_t argc);
bool Format(const char* fmt, const uint64_t* argv, size_t argc, std::string& output);

} // namespace detail
} // namespace dfx
} // namespace ccu
} // namespace AscendC

#endif // ASCCOMM_CCU_PRINT_INTERNAL_H
