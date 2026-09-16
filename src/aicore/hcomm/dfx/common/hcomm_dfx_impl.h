/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_COMMON_HCOMM_DFX_IMPL_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_COMMON_HCOMM_DFX_IMPL_H

#include "hcomm/hcomm_common.h"

#ifndef __aicore__
template <typename T>
struct LocalTensor {};
#endif

#include <cstdint>

#ifndef __aicore__
#include "acl/acl_rt.h"
#include <string>
#endif

namespace AscendC {

constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_MAGIC = 0x48434449U;
constexpr uint32_t HCOMM_DFX_CHANNEL_DUMP_VERSION = 2U;
// One snapshot contains one channel. Multi-channel callers invoke the API once per handle.
constexpr uint32_t HCOMM_DFX_MAX_CHANNELS = 1U;
constexpr uint32_t HCOMM_DFX_MAX_BUFFERS = 8U;
constexpr uint32_t HCOMM_DFX_MAX_QUEUE_CONTEXTS = 8U;
constexpr uint32_t HCOMM_DFX_QUEUE_ENTRY_BYTES = 4096U;
constexpr uint32_t HCOMM_DFX_CHANNEL_ENTITY_BYTES = 256U;
constexpr uint32_t HCOMM_DFX_BUFFER_BYTES = 64U;
constexpr uint32_t HCOMM_DFX_CONTEXT_BYTES = 128U;

enum HcommDfxQueueEntryType : uint32_t {
    HCOMM_DFX_ENTRY_URMA_WQE = 0,
    HCOMM_DFX_ENTRY_URMA_CQE = 1,
};

enum HcommDfxReadStatus : uint32_t {
    HCOMM_DFX_READ_OK = 0,
    HCOMM_DFX_READ_LAUNCH_FAIL = 1,
};

struct HcommDfxBufferDump {
    uint32_t rawSize;
    uint8_t raw[HCOMM_DFX_BUFFER_BYTES];
};

struct HcommDfxQueueCtxDump {
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
    uint32_t readStatus;
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
    HcommDfxBufferDump localBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxBufferDump remoteBuffers[HCOMM_DFX_MAX_BUFFERS];
    HcommDfxQueueCtxDump sqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
    HcommDfxQueueCtxDump cqContexts[HCOMM_DFX_MAX_QUEUE_CONTEXTS];
};

struct HcommDfxChannelDumpData {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t channelNum;
    HcommDfxChannelInfo channels[HCOMM_DFX_MAX_CHANNELS];
};
constexpr uint32_t HCOMM_DFX_QUEUE_READ_SUCCESS = 0U;
constexpr uint32_t HCOMM_DFX_QUEUE_READ_INVALID_PARAM = 1U;
constexpr uint32_t HCOMM_DFX_QUEUE_READ_FLAG_FLUSH_CHANNEL_CURSORS = 1U;

struct HcommDfxQueueReadArgs {
    uint64_t srcAddr;
    uint64_t resultAddr;
    uint32_t size;
    uint32_t flags;
};

struct HcommDfxQueueReadResult {
    uint32_t ret;
    uint32_t size;
    uint8_t data[HCOMM_DFX_QUEUE_ENTRY_BYTES];
};

#ifndef __aicore__
namespace HcommDfx {

void LogError(const char *format, ...);
void LogInfo(const char *format, ...);
void LogDumpText(const std::string &text);
void ExceptionCallback(aclrtExceptionInfo *exceptionInfo, void *userData);
bool HasPendingException();
bool IsExceptionCallbackBound();
void MarkExceptionCallbackBound();
void ClearPendingException();

int32_t DumpChannelInfoToString(uint32_t channelNum, const ChannelHandle *channels, std::string &out);

void *CreateQueueReader();
void DestroyQueueReader(void *reader);
bool ReadQueueEntry(void *reader, uint64_t addr, uint8_t *data, uint32_t size);

} // namespace HcommDfx
#endif // !__aicore__
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_COMMON_HCOMM_DFX_IMPL_H
