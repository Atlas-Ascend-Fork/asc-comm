/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cassert>
#include <cstdint>
#include "../../src/aicore/hcomm/dfx/host/hcomm_dfx_cursor.h"

int main()
{
    using AscendC::HcommDfx::ResolveAivSampleTail;
    uint32_t tail = 99;
    assert(ResolveAivSampleTail(0, 0, 0, 32768, tail) && tail == 0);
    assert(ResolveAivSampleTail(2, 0, 0, 32768, tail) && tail == 0);     // Nothing consumed.
    assert(ResolveAivSampleTail(2, 1, 2, 32768, tail) && tail == 2);     // Reported demo.
    assert(ResolveAivSampleTail(12, 11, 12, 32768, tail) && tail == 12); // MC2 log.
    assert(ResolveAivSampleTail(4, 1, 1, 32768, tail) && tail == 2);     // Two-BB completion.
    assert(ResolveAivSampleTail(32770, 32767, 7, 32768, tail) && tail == 32768);
    assert(ResolveAivSampleTail(65538, 65535, 7, 32768, tail) && tail == 65536);
    assert(ResolveAivSampleTail(1, 65535, 7, 32768, tail) && tail == 0); // uint32 wrap.
    assert(!ResolveAivSampleTail(2, 10, 1, 32768, tail));                // Inconsistent snapshot.
    assert(!ResolveAivSampleTail(65536, 0, 0, 32768, tail));             // Ambiguous CQ wrap.
    assert(!ResolveAivSampleTail(0, 0, 0, 0, tail));
    assert(!ResolveAivSampleTail(0, 0, 0, 131072, tail));
}
