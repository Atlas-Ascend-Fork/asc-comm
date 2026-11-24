/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_THREAD_NOTIFY_H
#define ASCCOMM_THREAD_NOTIFY_H
#include "hcomm_res_entity_defs.h"
namespace Asc {
constexpr uint32_t THREAD_NOTIFY_SEGMENT_SIZE = 127U;
// Keep linked-segment traversal out of the common first-segment replay path.
__attribute__((noinline)) inline const HcommRegedNotifyEntity* ResolveThreadNotifySegment(
    const HcommRegedNotifyEntity* segment, uint64_t index)
{
    do {
        const auto next = *reinterpret_cast<const uint64_t*>(segment + THREAD_NOTIFY_SEGMENT_SIZE);
        if (!next) {
            return nullptr;
        }
        segment = reinterpret_cast<const HcommRegedNotifyEntity*>(static_cast<uintptr_t>(next));
        index -= THREAD_NOTIFY_SEGMENT_SIZE;
    } while (index >= THREAD_NOTIFY_SEGMENT_SIZE);
    return segment + index;
}
inline const HcommRegedNotifyEntity* ResolveThreadNotify(const ThreadEntity& thread, uint64_t index)
{
    if (!thread.localNotifyAddr || index >= thread.localNotifyNum) {
        return nullptr;
    }
    if (index < THREAD_NOTIFY_SEGMENT_SIZE) {
        return thread.localNotifyAddr + index;
    }
    return ResolveThreadNotifySegment(thread.localNotifyAddr, index);
}
} // namespace Asc
#endif
