/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_COMMAND_REPLAY_H
#define ASCCOMM_COMMAND_REPLAY_H
#include "asccomm_primitives.h"

// Private versioned ABI. The caller owns the handle and all resource/buffer lifetimes,
// and serializes begin/end/replay/destroy. No handle or template is portable across libraries.
enum AsccommReplayActionV1 {
    ASCCOMM_REPLAY_CREATE = 0,
    ASCCOMM_REPLAY_BEGIN = 1,
    ASCCOMM_REPLAY_END = 2,
    ASCCOMM_REPLAY_EXECUTE = 3,
    ASCCOMM_REPLAY_DESTROY = 4,
    ASCCOMM_REPLAY_ABORT = 5,
    ASCCOMM_REPLAY_QUERY = 6
};
// Stable numeric values are consumed by devkit and diagnostic CSV parsers.
enum AsccommReplayStatus : uint32_t {
    ASCCOMM_BINARY_READY = 1,
    ASCCOMM_BINARY_UNAVAILABLE = 2,
    ASCCOMM_BINARY_FAILED = 4,
    ASCCOMM_BINARY_FALLBACK = 5
};
enum AsccommReplayCount : uint32_t {
    ASCCOMM_COUNT_COMMAND = 0,
    ASCCOMM_COUNT_SQE,
    ASCCOMM_COUNT_WQE,
    ASCCOMM_COUNT_DB,
    ASCCOMM_COUNT_SIZE
};
enum class AsccommRejectReason : uint32_t {
    Unknown = 0,
    EntryNotReady = 1,
    EntryBounds = 2,
    AdapterRejected = 3,
    PrepareException = 4,
    ScheduleCompile = 5,
    ApplyException = 6,
    Layout = 7,
    BatchEnd = 8,
    NotifyEncoding = 9,
    NotifyBinding = 10,
    NotifyChanged = 11,
    RtsqOrder = 12,
    RtsqEncoding = 13,
    ThreadBinding = 14,
    PendingSqe = 15,
    QueueChanged = 16,
    NestedTransport = 17,
    TransportResource = 18,
    UbOrder = 19,
    RdmaOrder = 20,
    UbPatchEncoding = 21,
    UbOpcode = 22,
    NotifyIndex = 23,
    RemoteNotifyRange = 24,
    RemoteBufferMissing = 25,
    RemoteBufferRange = 26,
    InlineProtection = 27,
    RemoteProtection = 28,
    InlinePayload = 29,
    LocalNotifyMissing = 30,
    LocalNotifyRange = 31,
    LocalProtection = 32,
    WriteNotifyProtection = 33,
    TransportEncoding = 34,
    ChannelQueue = 35,
    TransportBinding = 36,
    ThreadQueue = 37,
    PendingTransport = 38,
    UbQueue = 39,
    RdmaQueue = 40,
    ChannelNotifyEncoding = 41,
    ChannelNotifyIndex = 42,
    FenceEncoding = 43,
    FencePlan = 44,
    RdmaWqeEncoding = 45,
    RdmaPatchEncoding = 46,
    RdmaNotifyIndex = 47,
    RdmaNotifyRange = 48,
    RdmaProtection = 49,
    TransportEndEncoding = 50,
    UnsupportedEvent = 51,
    ResourceChanged = 52,
    ChannelNotifyChanged = 53,
    FenceChanged = 54,
    UbProtocol = 55,
    TransportEndBinding = 56,
    Count = 57
};
constexpr uint32_t ASCCOMM_REJECT_REASON_COUNT = static_cast<uint32_t>(AsccommRejectReason::Count);
constexpr uint64_t ASCCOMM_REJECT_MAGIC = 0x50460000ULL;
constexpr uint64_t ASCCOMM_REJECT_MASK = 0xffffULL;
constexpr uint64_t ASCCOMM_BINARY_ENTRY_BUDGET = 10ULL * 1024 * 1024;
typedef struct {
    uint64_t commandCount;
    uint64_t storageBytes;
    uint32_t ready;
    uint32_t reserved;
} AsccommReplayInfoV1;
#ifdef __cplusplus
extern "C" {
#endif
// Binary-only backend. BEGIN captures native SQE/WQE events, not primitive commands.
// EXECUTE preflights the whole entry. reserved=5 returns to the caller for ordinary
// algorithm expansion with no publication; partial execution errors cannot retry.
// info.reserved: 1 binary success/ready, 2 unavailable capture, 4 execution failure,
// 5 pre-publication fallback. counts[4]: primitive/SQE/WQE/DB counts per entry.
ASCCOMM_API int32_t
asccomm_binary_replay_v1(uint32_t action, void** handle, AsccommReplayInfoV1* info, uint64_t* counts);
#ifdef __cplusplus
}
#endif
#endif
