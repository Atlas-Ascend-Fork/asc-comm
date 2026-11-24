/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_SHARED_TAIL_H
#define ASCCOMM_SHARED_TAIL_H
#include <cstdint>
namespace AsccommSharedTail {
// Completion-slot attribution needs only these values, not the full RTSQ context.
struct Submission {
    uint32_t sqTail, sqDepth, pendingSqeCnt;
};
// Submission is externally serialized. This is not a concurrent-producer reservation.
// A successful wait guarantees space for all pending SQEs at the checked tail.
// Recursive submissions may move the shared tail and invalidate that guarantee.
template <class Queue, class Wait, class Space>
inline int Prepare(Queue& work, Queue& live, bool nonblocking, bool& ready, int invalid, Wait wait, Space space)
{
    ready = false;
    if (work.sqDepth < 2 || work.pendingSqeCnt >= work.sqDepth) {
        return invalid;
    }
    for (;;) {
        const uint32_t tail = live.sqTail;
        if (tail >= work.sqDepth) {
            return invalid;
        }
        work.sqTail = tail;
        if (work.sqHead >= work.sqDepth) {
            return invalid;
        }
        // No wait or recursive submission can occur on this path. The initial
        // acquire read is sufficient under the externally serialized writer contract.
        if (space(work) > work.pendingSqeCnt) {
            ready = true;
            return 0;
        }
        if (nonblocking) {
            return 0;
        }
        const int ret = wait(work);
        if (ret != 0) {
            return ret;
        }
        if (live.sqTail != tail) {
            continue;
        }
        ready = true;
        return 0;
    }
}
// Update the shared cursor only after the doorbell succeeds.
template <class Queue, class Doorbell>
inline auto Publish(Queue& live, uint32_t next, Doorbell doorbell) -> decltype(doorbell())
{
    auto result = doorbell();
    if (result == 0) {
        live.sqTail = next;
    }
    return result;
}
} // namespace AsccommSharedTail
#endif
