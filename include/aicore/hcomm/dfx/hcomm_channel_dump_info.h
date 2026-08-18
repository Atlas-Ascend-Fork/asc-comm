/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_INFO_H
#define ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_INFO_H

#include <cstdint>

namespace AscendC {

// Raw-first dump layout: the collector only saves the raw bytes of ChannelEntity /
// RegedBufferEntity / SqContext / CqContext / WQE / CQE plus a small envelope. All
// field decoding happens inside the framework at print time by reinterpreting these
// raw blocks against the internal hcomm structs, so this file intentionally carries
// no pre-extracted per-field mirrors.
constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_MAGIC = 0x48434449U;
constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_VERSION = 1U;
constexpr uint32_t HCOMM_DFX_MAX_CHANNELS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_BUFFERS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_QUEUE_CONTEXTS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_QUEUE_ENTRIES = 16U;
constexpr uint32_t HCOMM_DFX_QUEUE_ENTRY_BYTES = 256U;
constexpr uint32_t HCOMM_DFX_CHANNEL_ENTITY_BYTES = 256U;
constexpr uint32_t HCOMM_DFX_BUFFER_BYTES = 64U;
constexpr uint32_t HCOMM_DFX_CONTEXT_BYTES = 128U;

// Decoded kind of a sampled queue entry. URMA-only for now; RoCE entries are
// captured as raw bytes but not decoded.
enum HcommDfxQueueEntryType : uint32_t {
    HCOMM_DFX_ENTRY_URMA_WQE = 0,
    HCOMM_DFX_ENTRY_URMA_CQE = 1,
};

// Why a sampled entry's payload is (not) present, so the parser can explain an
// empty slot instead of silently printing nothing.
enum HcommDfxReadStatus : uint32_t {
    HCOMM_DFX_READ_OK = 0,          // device-side helper read the entry successfully
    HCOMM_DFX_READ_LAUNCH_FAIL = 1, // helper kernel launch / sync / D2H copy failed
};

// Raw RegedBufferEntity bytes.
struct HcommDfxBufferDump {
    uint32_t rawSize;
    uint8_t raw[HCOMM_DFX_BUFFER_BYTES];
};

// Raw SqContext/CqContext bytes plus the live head/tail counters. The counters are
// dereferenced from the context's headAddr/tailAddr at dump time and cannot be
// recovered from the raw bytes alone (those only hold the pointers).
struct HcommDfxQueueCtxDump {
    uint32_t head;      // live counter read from headAddr
    uint32_t tail;      // live counter read from tailAddr
    uint32_t headValid; // 1 = head read succeeded
    uint32_t tailValid; // 1 = tail read succeeded
    uint32_t rawSize;
    uint8_t raw[HCOMM_DFX_CONTEXT_BYTES];
};

// Raw WQE/CQE bytes plus the envelope needed to locate and explain the sample.
struct HcommDfxQueueEntry {
    uint32_t entryType;  // HcommDfxQueueEntryType
    uint32_t queueIndex; // owning SQ/CQ context index
    uint32_t entryIndex; // slot index within the queue
    uint32_t cursor;     // logical producer/consumer cursor
    uint64_t addr;       // device VA the entry was read from
    uint32_t size;       // bytes captured in data
    uint32_t readStatus; // HcommDfxReadStatus
    uint8_t data[HCOMM_DFX_QUEUE_ENTRY_BYTES];
};

struct HcommDfxChannelInfo {
    uint64_t handle;
    uint32_t valid;
    uint32_t channelEntitySize;
    uint8_t channelEntity[HCOMM_DFX_CHANNEL_ENTITY_BYTES];
    uint32_t localBufferDumpNum;
    uint32_t remoteBufferDumpNum;
    uint32_t sqContextDumpNum;
    uint32_t cqContextDumpNum;
    uint32_t queueEntryDumpNum;
    uint32_t reserved;
    HcommDfxBufferDump localBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxBufferDump remoteBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxQueueCtxDump sqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
    HcommDfxQueueCtxDump cqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
    HcommDfxQueueEntry queueEntries[HCOMM_DFX_MAX_QUEUE_ENTRIES];
};

struct HcommDfxChannelDumpData {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t channelNum;
    HcommDfxChannelInfo channels[HCOMM_DFX_MAX_CHANNELS];
};

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_INFO_H
