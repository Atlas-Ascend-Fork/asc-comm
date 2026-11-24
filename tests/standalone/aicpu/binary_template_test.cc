/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../../src/aicpu/asccomm/primitives/task_cache/binary_template.h"
#include <cassert>
#include <stdexcept>
using namespace AsccommBinaryCache;
struct Adapter {
    int prepared = 0, applied = 0;
    bool reject = false, fail = false, throws = false;
    uint32_t reason = 0;
    bool Prepare(const Event&, const Entry&)
    {
        ++prepared;
        return !reject || (reason ? Reject(static_cast<AsccommRejectReason>(reason)) : false);
    }
    bool Apply(const Event&, const Entry&)
    {
        assert(prepared == 2);
        ++applied;
        if (throws) {
            throw std::runtime_error("partial publication");
        }
        return !fail;
    }
};
Entry Make()
{
    Entry e;
    uint64_t payload = 17;
    assert(e.Append(Kind::Rtsq, 0, 1, &payload, sizeof(payload), {}, 4096));
    assert(e.Append(Kind::BatchEnd, 0, 1, nullptr, 0, {}, 4096));
    assert(e.Seal(true));
    return e;
}
int main()
{
    Ranges ranges{{0x1000, 0x100, 0x2000, 0x100}};
    assert(ValidRanges(ranges));
    InvocationRanges() = &ranges;
    Entry moved;
    moved.Reset();
    InvocationRanges() = nullptr;
    moved.replayRanges = {{0x3000, 0x100, 0x4000, 0x100}};
    uint64_t mapped = 0;
    assert(moved.MapAddress(0x1010, 16, mapped) && mapped == 0x3010);
    assert(moved.MapAddress(0x2020, 32, mapped) && mapped == 0x4020);
    assert(!moved.MapAddress(0x10f0, 32, mapped));
    assert(!moved.MapAddress(0xff0, 32, mapped));
    assert(!moved.MapAddress(0x3000, 16, mapped)); // fixed resource would alias relocated input
    assert(moved.MapAddress(0x5000, 16, mapped) && mapped == 0x5000);
    assert(!ValidRanges(Ranges{{UINT64_MAX - 8, 16, 0x1000, 256}}));
    assert(!ValidRanges(Ranges{{0x1000, 256, 0x1080, 256}}));
    auto e = Make();
    Adapter a;
    assert(Replay(e, 0, a) == Result::Complete);
    assert(a.applied == 2);
    auto original = e.bytes;
    Adapter b;
    b.reject = true;
    assert(Replay(e, 0, b) == Result::Fallback);
    assert(b.applied == 0);
    assert(RejectReason() == 3);
    Adapter specific;
    specific.reject = true;
    specific.reason = 38;
    assert(Replay(e, 0, specific) == Result::Fallback);
    assert(RejectReason() == 38); // generic fallback must not overwrite first cause
    assert(specific.applied == 0);
    Adapter success;
    assert(Replay(e, 0, success) == Result::Complete);
    assert(RejectReason() == 0); // previous failure must not leak into next replay
    Adapter c;
    c.fail = true;
    assert(Replay(e, 0, c) == Result::Failed);
    assert(c.applied == 1);
    assert(e.state == State::Invalid);
    e = Make();
    Adapter d;
    d.throws = true;
    assert(Replay(e, 0, d) == Result::Failed);
    assert(d.applied == 1);
    e = Make();
    Adapter f;
    assert(Replay(e, 1, f) == Result::Fallback);
    assert(RejectReason() == 1);
    assert(f.applied == 0);
    e.events[0].length = UINT32_MAX;
    assert(Replay(e, 0, f) == Result::Fallback);
    assert(RejectReason() == 2);
    assert(f.applied == 0);
    Entry bad;
    assert(!bad.Append(Kind::Rtsq, 0, 1, nullptr, 8, {}, 4096));
    assert(!bad.Seal(true));
    Entry budget;
    uint64_t p = 0;
    assert(!budget.Append(Kind::Rtsq, 0, 1, &p, 8, {}, 1));
    assert(original == Make().bytes);
    Entry fences;
    uint64_t on[2] = {0, 1}, off[2] = {1, 0};
    assert(fences.Append(Kind::Fence, 42, VERSION, on, 16, {}, 4096));
    assert(fences.Append(Kind::Fence, 42, VERSION, off, 16, {}, 4096));
    assert(fences.Append(Kind::BatchEnd, 0, VERSION, nullptr, 0, {}, 4096));
    assert(fences.Seal(true));
    assert(fences.plan[1].previousFence == 0);
    assert(fences.resources.size() == 2);
    auto capacity = fences.workspace.capacity();
    fences.Reset();
    assert(!fences.compiled && fences.workspace.capacity() == capacity);
    assert(fences.Append(Kind::Fence, 42, VERSION, on, 16, {}, 4096));
    assert(fences.Append(Kind::Fence, 42, VERSION, on, 16, {}, 4096));
    assert(fences.Append(Kind::BatchEnd, 0, VERSION, nullptr, 0, {}, 4096));
    assert(!fences.Seal(true)); // inconsistent internal transition cannot be binary ready

    // Compare compiled address roles against the original checked mapper, including
    // a fixed resource operand aliased by a relocated user buffer and range edges.
    Entry mapping;
    mapping.relocatable = true;
    mapping.captureRanges = {0x1000, 0x100, 0x3000, 0x200};
    for (uint64_t base : {0x1000U, 0x5000U, 0x7000U}) {
        mapping.replayRanges = {base, 0x100, base + 0x2000, 0x200};
        for (uint64_t addr = 0xf00; addr < 0x9400; addr += 17) {
            for (uint64_t length : {0U, 1U, 16U, 128U, 256U, 512U}) {
                AddressPlan plan;
                const bool built = plan.Compile(mapping.captureRanges, true, addr, length);
                uint64_t compiled = 0, original = 0;
                const bool mapped = mapping.MapAddress(addr, length, original);
                const bool resolved =
                    built && plan.Resolve(mapping.captureRanges, mapping.replayRanges, true, compiled);
                assert(resolved == mapped);
                if (resolved) {
                    assert(compiled == original);
                }
            }
        }
    }
}
