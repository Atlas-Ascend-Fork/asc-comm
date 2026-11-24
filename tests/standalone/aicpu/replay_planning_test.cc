/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../../src/aicpu/asccomm/primitives/task_cache/replay_protection.h"
#include "../../../src/aicpu/asccomm/primitives/task_cache/replay_schedule.h"
#include "../../../src/aicpu/asccomm/resource/common/asccomm_thread_notify.h"
#include <cassert>

using namespace AsccommBinaryCache;

int main()
{
    Entry entry;
    entry.events = {
        {Kind::UbBegin, 1, 0, 0, 0, 0, 0}, {Kind::UbWqe, 1, 0, 0, 64, 0, 0}, {Kind::UbEnd, 1, 0, 0, 8, 0, 0}};
    entry.plan.resize(entry.events.size());
    assert(CompileReplaySchedule(entry));
    assert(entry.segments.size() == 1 && entry.segments[0].count == 3);
    entry.events.back().resource = 2;
    assert(!CompileReplaySchedule(entry));

    HcommRegedBufferEntity buffer{};
    buffer.type = HCOMM_REGED_BUFFER_RMA;
    auto& region = buffer.bufferInfo.rma;
    region.addr = 1024;
    region.size = 512;
    region.protectionInfo.type = HCOMM_PROTECTION_TYPE_URMA;
    region.protectionInfo.memInfo.ub.tokenId = 11;
    region.protectionInfo.memInfo.ub.tokenValue = 12;
    ChannelEntity channel{};
    channel.localBufferAddr = &buffer;
    channel.localBufferNum = 1;
    ReplayProtection work;
    ReplayProtection::ProtectionRef ref{};
    ref.kind = ReplayProtection::ProtectionKind::Local;
    ref.address = 1152;
    ref.length = 32;
    work.BindRegistration(entry, channel, ref);
    assert(ref.registration == 0);
    ref.address += 32;
    work.BindRegistration(entry, channel, ref);
    assert(work.registrations.size() == 1);
    ++work.generation;
    uint32_t token = 0, value = 0;
    assert(work.RegisteredProtection(channel, ref, ref.address, token, value));
    assert(token == 11 && value == 12);
    ThreadEntity thread{};
    assert(Asc::ResolveThreadNotify(thread, 0) == nullptr);
}
