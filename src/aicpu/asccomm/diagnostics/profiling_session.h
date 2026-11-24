/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_PROFILING_SESSION_H
#define ASCCOMM_PROFILING_SESSION_H
#include "asccomm_profiling_internal.h"
namespace AscProf {
extern thread_local bool enabled;
inline bool Enabled() noexcept { return enabled; }
enum class TaskName : uint8_t {
    Unknown,
    Memcpy,
    ReduceInline,
    NotifyWait,
    NotifyRecord,
    CcoreWait,
    CcoreRecord,
    UbInlineWrite,
    UbReduceInline,
    UbWriteOrRead,
    ReduceWithNotify,
    WriteWithNotify,
    Count
};
// Immutable wire classification only. Addresses, PI, channel binding and
// profiling IDs always come from the current invocation.
struct WqeDescription {
    TaskName name = TaskName::Unknown;
    uint32_t localOffset = 0;
    uint32_t opType = 0;
    bool inlineData = false, read = false, notify = false;
};
bool DescribeWqe(const void* bytes, uint32_t length, WqeDescription& description) noexcept;
void Wqe(
    uint64_t channel, const void* bytes, uint32_t length, uint16_t endPi,
    const WqeDescription* description = nullptr) noexcept;
// Restore source bits after queue copy, including failures: cached templates stay immutable.
class SqeProfileScope {
public:
    SqeProfileScope(void* bytes, uint32_t count) : bytes_(bytes), count_(0)
    {
        if (Enabled()) {
            Prepare(count);
        }
    }
    ~SqeProfileScope()
    {
        if (count_) {
            Restore();
        }
    }
    SqeProfileScope(const SqeProfileScope&) = delete;
    SqeProfileScope& operator=(const SqeProfileScope&) = delete;

private:
    void Prepare(uint32_t count);
    void Restore();
    void* bytes_;
    uint32_t count_;
};
// Snapshot submitted bytes during a bounded binary publication window. Decode
// after the next publication or at operation end; never retain template pointers.
class PublicationScope {
public:
    explicit PublicationScope(uint32_t instructionCount) noexcept : owner_(Enabled() && Begin(instructionCount)) {}
    ~PublicationScope()
    {
        if (owner_) {
            End();
        }
    }
    PublicationScope(const PublicationScope&) = delete;
    PublicationScope& operator=(const PublicationScope&) = delete;

private:
    static bool Begin(uint32_t instructionCount) noexcept;
    static void End() noexcept;
    bool owner_{false};
};
void Submitted(uint32_t sq, const void* bytes, uint32_t count) noexcept;
void Condition(uint32_t sq, uint32_t task, bool wait) noexcept;
} // namespace AscProf
#endif
