/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../../src/aicpu/asccomm/resource/common/asccomm_shared_tail.h"
#include <cassert>
#include <cstdint>
struct Queue {
    uint32_t sqTail, sqHead, sqDepth, pendingSqeCnt;
};
uint32_t Space(const Queue& q) { return q.sqTail >= q.sqHead ? q.sqDepth - q.sqTail + q.sqHead : q.sqHead - q.sqTail; }
int main()
{
    Queue live{6, 5, 8, 0}, work = live;
    work.pendingSqeCnt = 3;
    unsigned waits = 0;
    auto wait = [&](Queue&) {
        ++waits;
        return 0;
    };
    bool ready = false;
    assert(AsccommSharedTail::Prepare(work, live, false, ready, -1, wait, Space) == 0 && ready);
    assert(waits == 0 && work.sqTail == 6);
    // Publication crosses ring end. Doorbell observes the previous live cursor;
    // work remains at the submission origin for completion-slot bookkeeping.
    const AsccommSharedTail::Submission submitted{work.sqTail, work.sqDepth, work.pendingSqeCnt};
    static_assert(sizeof(submitted) == 3 * sizeof(uint32_t), "compact completion origin");
    assert(AsccommSharedTail::Publish(live, 1, [&] {
               assert(live.sqTail == 6);
               assert(work.sqTail == 6);
               return 0;
           }) == 0);
    assert(live.sqTail == 1);
    assert((submitted.sqTail + 2) % submitted.sqDepth == 0);
    // A nested submission during space wait changes shared tail: repeat validation.
    live = {6, 0, 8, 0};
    work = live;
    work.pendingSqeCnt = 2;
    waits = 0;
    auto nested = [&](Queue& q) {
        if (++waits == 1) {
            live.sqTail = 7;
        } else {
            assert(q.sqTail == 7);
            q.sqHead = 4;
        }
        return 0;
    };
    assert(AsccommSharedTail::Prepare(work, live, false, ready, -1, nested, Space) == 0 && ready && waits == 2);
    // Native work aliases the ledger.
    live = {6, 0, 8, 2};
    waits = 0;
    assert(AsccommSharedTail::Prepare(live, live, false, ready, -1, nested, Space) == 0 && ready && waits == 2);
    live = {8, 0, 8, 1};
    work = live;
    assert(AsccommSharedTail::Prepare(work, live, false, ready, -1, wait, Space) == -1);
    // Nonblocking full queue does not wait, publish, or claim a successful submit.
    live = {6, 0, 8, 2};
    work = live;
    waits = 0;
    assert(AsccommSharedTail::Prepare(work, live, true, ready, -1, wait, Space) == 0 && !ready && waits == 0);
    auto timeout = [](Queue&) { return -9; };
    assert(AsccommSharedTail::Prepare(work, live, false, ready, -1, timeout, Space) == -9 && !ready);
    work.sqDepth = 0;
    assert(AsccommSharedTail::Prepare(work, live, false, ready, -1, wait, Space) == -1);
    // Ambiguous hardware acceptance: propagate error without ledger rollback/retry.
    unsigned bells = 0;
    assert(AsccommSharedTail::Publish(live, 3, [&] {
               ++bells;
               assert(live.sqTail == 6);
               return -8;
           }) == -8);
    assert(bells == 1 && live.sqTail == 6);
}
