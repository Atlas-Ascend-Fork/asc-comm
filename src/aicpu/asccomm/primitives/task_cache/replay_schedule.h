/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#pragma once
#include <cstring>
#include "binary_template.h"
#include "hcomm_res_entity_defs.h"
namespace AsccommBinaryCache {
struct BinaryRtsqTask {
    uint64_t context, local, base;
    uint32_t depth, sqId, streamId;
    uint8_t sqe[HCOMM_RESOURCE_RTSQ_SQE_SIZE];
};

inline bool CompileReplaySchedule(Entry& entry)
{
    entry.checks.clear();
    entry.segments.clear();
    uint64_t transport = 0;
    for (uint32_t i = 0; i < entry.events.size(); ++i) {
        const auto& e = entry.events[i];
        bool check = true;
        switch (e.kind) {
            case Kind::UbBegin:
                if (transport || !e.resource) {
                    return false;
                }
                transport = e.resource;
                break;
            case Kind::UbWqe:
                if (transport != e.resource || !transport) {
                    return false;
                }
                check = false;
                break;
            case Kind::UbEnd:
                if (transport != e.resource || !transport || e.length != 8) {
                    return false;
                }
                transport = 0;
                check = false;
                break;
            case Kind::Rtsq:
                if (transport || !e.resource || e.length != sizeof(BinaryRtsqTask)) {
                    return false;
                }
                check = false;
                break;
            case Kind::Submit:
                if (transport || !e.resource || e.length != sizeof(uint32_t)) {
                    return false;
                }
                check = false;
                break;
            case Kind::Launch:
                if (transport || !e.resource || e.length) {
                    return false;
                }
                check = false;
                break;
            case Kind::ThreadNotifyGuard:
                if (transport || !e.resource || e.length != 16) {
                    return false;
                }
                break;
            case Kind::ChannelNotifyGuard:
                if (!e.resource || e.length != 16) {
                    return false;
                }
                break;
            case Kind::Fence:
                check = entry.plan[i].previousFence == UINT32_MAX;
                break;
            case Kind::BatchFlush:
                if (transport || e.length) {
                    return false;
                }
                check = false;
                break;
            case Kind::BatchEnd:
                if (transport || e.length || i + 1 != entry.events.size()) {
                    return false;
                }
                check = false;
                break;
            default:
                return false;
        }
        if (check && e.kind != Kind::Rtsq && e.kind != Kind::UbWqe) {
            for (const auto old : entry.checks) {
                const auto& previous = entry.events[old];
                if (e.kind == Kind::UbBegin && previous.kind == e.kind && previous.resource == e.resource) {
                    if (previous.length != e.length || e.length < 16 ||
                        std::memcmp(
                            entry.bytes.data() + previous.offset + 16, entry.bytes.data() + e.offset + 16,
                            e.length - 16)) {
                        return false;
                    }
                    check = false;
                    break;
                }
                if (previous.kind == e.kind && previous.resource == e.resource && previous.length == e.length &&
                    (!e.length ||
                     std::memcmp(entry.bytes.data() + previous.offset, entry.bytes.data() + e.offset, e.length) == 0)) {
                    check = false;
                    break;
                }
            }
        }
        if (check) {
            entry.checks.push_back(i);
        }
        if (e.kind == Kind::ThreadNotifyGuard || e.kind == Kind::ChannelNotifyGuard) {
            continue;
        }
        if (!entry.segments.empty() && (e.kind == Kind::Rtsq || e.kind == Kind::UbWqe)) {
            auto& last = entry.segments.back();
            if (last.kind == e.kind && last.first + last.count == i &&
                entry.events[last.first].resource == e.resource) {
                ++last.count;
                continue;
            }
        }
        entry.segments.push_back({e.kind, i, 1});
    }
    // Bind one UB array including its optional fence transition. The transition
    // keeps its original position before WQEs; unrelated boundaries are not crossed.
    size_t out = 0;
    for (size_t i = 0; i < entry.segments.size(); ++i) {
        auto segment = entry.segments[i];
        size_t next = i + 1;
        if (segment.kind == Kind::UbBegin && next < entry.segments.size()) {
            if (entry.segments[next].kind == Kind::Fence && entry.segments[next].first == segment.first + 1) {
                segment.fence = entry.segments[next++].first;
            }
            if (next + 1 < entry.segments.size()) {
                const auto& wqe = entry.segments[next];
                const auto& end = entry.segments[next + 1];
                const auto prefix = segment.fence == UINT32_MAX ? 1U : 2U;
                if (wqe.kind == Kind::UbWqe && end.kind == Kind::UbEnd && wqe.first == segment.first + prefix &&
                    end.first == wqe.first + wqe.count) {
                    segment.count = wqe.count + prefix + 1;
                    i = next + 1;
                } else {
                    segment.fence = UINT32_MAX;
                }
            } else {
                segment.fence = UINT32_MAX;
            }
        }
        entry.segments[out++] = segment;
    }
    entry.segments.resize(out);
    return !transport;
}
} // namespace AsccommBinaryCache
