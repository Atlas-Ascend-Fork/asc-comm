/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_UB_QUEUE_BUDGET_H
#define ASCCOMM_UB_QUEUE_BUDGET_H
#include <cstdint>
namespace Asc {
struct UbQueueBudget {
    uint32_t depth = 0, used = 0;
    uint16_t expectedPi = 0, ci = 0;
    bool initialized = false, failed = false;
    bool Matches(uint16_t pi) const { return !failed && (!initialized || pi == expectedPi); }
    bool HasSpace(uint32_t count) const { return count && count <= depth && used <= depth - count; }
    // Only after a hardware completion proof, never merely after a DB enqueue.
    void Completed(uint16_t pi)
    {
        expectedPi = ci = pi;
        used = 0;
        initialized = true;
    }
    bool Reserve(uint16_t pi, uint32_t count)
    {
        if (!initialized || !Matches(pi) || !HasSpace(count)) {
            return false;
        }
        used += count;
        expectedPi = static_cast<uint16_t>(pi + count);
        return true;
    }
};
// Ordered shared writers: PI/CI come from the entity, while locally proven
// completion may be newer than the entity CI (asccomm does not overwrite it).
// UbQueueBudget supplies common reservation arithmetic; all runtime bindings
// use this shared-state adapter before reserving capacity.
struct SharedUbQueueBudget : UbQueueBudget {
    bool needsCompletion = false;
    bool Refresh(uint16_t pi, uint16_t sharedCi)
    {
        if (failed || !depth || depth > UINT16_MAX) {
            return false;
        }
        if (!initialized || pi != expectedPi) {
            // External publication is normal. Import outstanding WQEBBs, not
            // an empty queue. An ambiguous/stale CI forces a completion proof.
            const uint32_t outstanding = static_cast<uint16_t>(pi - sharedCi);
            expectedPi = pi;
            ci = sharedCi;
            initialized = true;
            needsCompletion = outstanding > depth;
            used = needsCompletion ? depth : outstanding;
        } else if (!needsCompletion) {
            // Do not regress a locally confirmed CI to a stale shared value,
            // or accept a completion beyond this publication window.
            const uint32_t completed = static_cast<uint16_t>(sharedCi - ci);
            if (completed <= used) {
                used -= completed;
                ci = sharedCi;
            }
        }
        return true;
    }
    void Completed(uint16_t pi)
    {
        UbQueueBudget::Completed(pi);
        needsCompletion = false;
    }
};

} // namespace Asc
#endif
