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

constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_MAGIC = 0x48434449U;
constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_VERSION = 1U;
constexpr uint32_t HCOMM_DFX_MAX_CHANNELS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_BUFFERS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_QUEUE_CONTEXTS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_QUEUE_ENTRIES = 16U;
constexpr uint32_t HCOMM_DFX_QUEUE_ENTRY_BYTES = 256U;
constexpr uint32_t HCOMM_DFX_CHANNEL_ENTITY_BYTES = 256U;
constexpr uint32_t HCOMM_DFX_CONTEXT_BYTES = 128U;
constexpr uint32_t HCOMM_DFX_QUEUE_READ_SUCCESS = 0U;
constexpr uint32_t HCOMM_DFX_QUEUE_READ_INVALID_PARAM = 1U;

enum HcommDfxQueueEntryType : uint32_t {
    HCOMM_DFX_ENTRY_URMA_WQE = 0,
    HCOMM_DFX_ENTRY_URMA_CQE = 1,
};

struct HcommDfxBufferInfo {
    int32_t type;
    uint64_t addr;
    uint64_t size;
};

struct HcommDfxQueueContext {
    int32_t type;
    uint64_t va;
    uint64_t headAddr;
    uint64_t tailAddr;
    uint64_t dbVa;
    uint32_t depth;
    uint32_t entrySize;
    uint32_t head;
    uint32_t tail;
    uint32_t headValid;
    uint32_t tailValid;
    uint32_t rawSize;
    uint8_t raw[HCOMM_DFX_CONTEXT_BYTES];
};

struct HcommDfxQueueEntry {
    uint32_t entryType;
    uint32_t queueIndex;
    uint32_t entryIndex;
    uint32_t cursor;
    uint64_t addr;
    uint32_t size;
    uint32_t valid;
    uint8_t data[HCOMM_DFX_QUEUE_ENTRY_BYTES];
};

struct HcommDfxChannelInfo {
    uint64_t handle;
    uint32_t valid;
    uint32_t channelEntitySize;
    uint8_t channelEntity[HCOMM_DFX_CHANNEL_ENTITY_BYTES];
    int32_t engine;
    int32_t protocol;
    uint32_t localBufferNum;
    uint32_t remoteBufferNum;
    uint32_t sqNum;
    uint32_t cqNum;
    uint32_t sqHead;
    uint32_t sqTail;
    uint32_t cqHead;
    uint32_t cqTail;
    uint32_t localBufferDumpNum;
    uint32_t remoteBufferDumpNum;
    uint32_t sqContextDumpNum;
    uint32_t cqContextDumpNum;
    uint32_t queueEntryDumpNum;
    HcommDfxBufferInfo localBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxBufferInfo remoteBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxQueueContext sqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
    HcommDfxQueueContext cqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
    HcommDfxQueueEntry queueEntries[HCOMM_DFX_MAX_QUEUE_ENTRIES];
};

struct HcommDfxChannelDumpData {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t channelNum;
    HcommDfxChannelInfo channels[HCOMM_DFX_MAX_CHANNELS];
};

struct HcommDfxQueueReadArgs {
    uint64_t srcAddr;
    uint64_t resultAddr;
    uint32_t size;
    uint32_t reserved;
};

struct HcommDfxQueueReadResult {
    uint32_t ret;
    uint32_t size;
    uint8_t data[HCOMM_DFX_QUEUE_ENTRY_BYTES];
};

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_INFO_H
