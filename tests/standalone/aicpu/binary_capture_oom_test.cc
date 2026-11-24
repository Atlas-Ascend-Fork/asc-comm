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
#include <cstdlib>
#include <new>
#include <exception>
#include "../../../src/aicpu/asccomm/primitives/task_cache/binary_template.h"
namespace {
bool failAllocation = false;
}
void* operator new(std::size_t bytes)
{
    if (failAllocation) {
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(bytes ? bytes : 1)) {
        return result;
    }
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
int main()
{
    using namespace AsccommBinaryCache;
    std::set_terminate([] { std::_Exit(86); });
    Entry entry;
    Capture() = &entry;
    Channel() = 1;
    char bytes[64]{};
    failAllocation = true;
    CaptureWqe(Kind::UbWqe, bytes, sizeof(bytes));
    failAllocation = false;
    assert(entry.state == State::Invalid);
    Capture() = nullptr;
    // Preallocated capture requires no temporary Patch-vector allocation.
    Entry reused;
    reused.bytes.reserve(128);
    reused.events.reserve(2);
    reused.patches.reserve(2);
    Capture() = &reused;
    failAllocation = true;
    CaptureWqe(Kind::UbWqe, bytes, sizeof(bytes));
    failAllocation = false;
    assert(reused.state == State::Capturing && reused.events.size() == 1);
    Capture() = nullptr;
}
