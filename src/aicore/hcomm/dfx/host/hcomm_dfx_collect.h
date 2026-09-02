/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_COLLECT_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_COLLECT_H

#include <algorithm>
#include <cstring>
#include <new>

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include "../impl/hcomm_dfx_queue_reader_kernel.h"
#include "hcomm_dfx_exception.h"
#include "hcomm_dfx_parse.h"

namespace AscendC {
namespace HcommDfx {

struct QueueReadContext {
    void *deviceResult = nullptr;
    aclrtStream stream = nullptr;
};

constexpr uint32_t QUEUE_LEADING_WINDOW = 4U;
constexpr uint32_t QUEUE_TAIL_WINDOW = 4U;

inline uint64_t PtrToU64(const void *ptr)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
}

inline bool CopyFromDevice(void *dst, uint32_t dstSize, uint64_t src, uint32_t size)
{
    if (dst == nullptr || dstSize == 0U) {
        return false;
    }
    if (src == 0U || size == 0U) {
        std::memset(dst, 0, dstSize);
        return false;
    }
    const uint32_t copySize = std::min(dstSize, size);
    std::memset(dst, 0, dstSize);
    return aclrtMemcpy(dst, copySize, reinterpret_cast<void *>(src), copySize, ACL_MEMCPY_DEVICE_TO_HOST) == ACL_SUCCESS;
}

template <typename T>
inline bool CopyFromDevice(uint64_t src, T &dst, const char *tag)
{
    (void)tag;
    return CopyFromDevice(&dst, static_cast<uint32_t>(sizeof(T)), src, static_cast<uint32_t>(sizeof(T)));
}

inline uint32_t SqWqeReadBytes(uint32_t wqeSize)
{
    uint32_t bytes = wqeSize <= HCOMM_DFX_QUEUE_ENTRY_BYTES / 2U ? wqeSize * 2U : wqeSize;
    return std::min<uint32_t>(bytes, HCOMM_DFX_QUEUE_ENTRY_BYTES);
}

inline void *CreateQueueReader()
{
    auto *ctx = new (std::nothrow) QueueReadContext;
    if (ctx == nullptr) {
        Trace("new queue read context failed");
        return nullptr;
    }
    aclError ret = aclrtMalloc(&ctx->deviceResult, sizeof(HcommDfxQueueReadResult), ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtMalloc queue read result failed ret=%d", ret);
        delete ctx;
        return nullptr;
    }

    ret = aclrtCreateStream(&ctx->stream);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtCreateStream queue read failed ret=%d", ret);
        (void)aclrtFree(ctx->deviceResult);
        delete ctx;
        return nullptr;
    }
    return ctx;
}

inline void DestroyQueueReader(void *reader)
{
    auto *ctx = reinterpret_cast<QueueReadContext *>(reader);
    if (ctx == nullptr) {
        return;
    }
    if (ctx->stream != nullptr) {
        (void)aclrtDestroyStream(ctx->stream);
    }
    if (ctx->deviceResult != nullptr) {
        (void)aclrtFree(ctx->deviceResult);
    }
    delete ctx;
}

inline bool ReadQueueEntry(void *reader, uint64_t addr, uint8_t *data, uint32_t size)
{
    auto *ctx = reinterpret_cast<QueueReadContext *>(reader);
    if (ctx == nullptr || addr == 0 || data == nullptr || size == 0 || size > HCOMM_DFX_QUEUE_ENTRY_BYTES) {
        return false;
    }

    HcommDfxQueueReadArgs args = {addr, reinterpret_cast<uint64_t>(ctx->deviceResult), size, 0};
    hcomm_dfx_queue_read_kernel<<<1, 0, ctx->stream>>>(args);
    aclError ret = aclrtSynchronizeStream(ctx->stream);

    HcommDfxQueueReadResult result;
    std::memset(&result, 0, sizeof(result));
    if (ret == ACL_SUCCESS) {
        ret = aclrtMemcpy(&result, sizeof(result), ctx->deviceResult, sizeof(result), ACL_MEMCPY_DEVICE_TO_HOST);
    }

    if (ret != ACL_SUCCESS || result.ret != HCOMM_DFX_QUEUE_READ_SUCCESS || result.size != size) {
        Trace("queue read failed addr=0x%llx ret=%d entryRet=%u size=%u expect=%u",
            static_cast<unsigned long long>(addr), ret, result.ret, result.size, size);
        return false;
    }

    std::memcpy(data, result.data, size);
    return true;
}

inline bool AppendQueueEntry(void *reader, uint32_t entryType, uint32_t queueIndex, uint32_t entryIndex,
                             uint32_t cursor, uint64_t addr, uint32_t size, HcommDfxChannelInfo &out)
{
    if (out.queueEntryDumpNum >= HCOMM_DFX_MAX_QUEUE_ENTRIES || addr == 0 || size == 0) {
        return false;
    }
    for (uint32_t idx = 0; idx < out.queueEntryDumpNum; ++idx) {
        const HcommDfxQueueEntry &entry = out.queueEntries[idx];
        if (entry.entryType == entryType && entry.queueIndex == queueIndex && entry.entryIndex == entryIndex &&
            entry.addr == addr) {
            return entry.readStatus == HCOMM_DFX_READ_OK;
        }
    }
    HcommDfxQueueEntry &entry = out.queueEntries[out.queueEntryDumpNum++];
    entry.entryType = entryType;
    entry.queueIndex = queueIndex;
    entry.entryIndex = entryIndex;
    entry.cursor = cursor;
    entry.addr = addr;
    entry.size = std::min<uint32_t>(size, HCOMM_DFX_QUEUE_ENTRY_BYTES);
    entry.readStatus = ReadQueueEntry(reader, addr, entry.data, entry.size) ? HCOMM_DFX_READ_OK :
        HCOMM_DFX_READ_LAUNCH_FAIL;
    return entry.readStatus == HCOMM_DFX_READ_OK;
}

inline void CollectBuffers(uint64_t addr, uint32_t num, HcommDfxBufferDump *buffers, uint32_t &dumpNum)
{
    dumpNum = 0;
    if (addr == 0 || buffers == nullptr) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(num, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        RegedBufferEntity buffer;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(RegedBufferEntity), buffer,
                "RegedBufferEntity")) {
            continue;
        }
        HcommDfxBufferDump &out = buffers[dumpNum++];
        out.rawSize = std::min<uint32_t>(sizeof(RegedBufferEntity), HCOMM_DFX_BUFFER_BYTES);
        std::memcpy(out.raw, &buffer, out.rawSize);
    }
}

inline void SampleSqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t wqeSize, uint32_t head,
    HcommDfxChannelInfo &channel, void *reader)
{
    const uint32_t readBytes = SqWqeReadBytes(wqeSize);
    const uint32_t leading = std::min<uint32_t>(depth, QUEUE_LEADING_WINDOW);
    for (uint32_t idx = 0; idx < leading; ++idx) {
        (void)AppendQueueEntry(reader, HCOMM_DFX_ENTRY_URMA_WQE, queueIndex, idx, idx,
            va + static_cast<uint64_t>(idx) * wqeSize, readBytes, channel);
    }
    const uint32_t window = head == 0 ? 1U : std::min<uint32_t>(head, QUEUE_TAIL_WINDOW);
    const uint32_t start = head >= window ? head - window : 0U;
    for (uint32_t idx = 0; idx < window; ++idx) {
        const uint32_t slot = (start + idx) % depth;
        (void)AppendQueueEntry(reader, HCOMM_DFX_ENTRY_URMA_WQE, queueIndex, slot, start + idx,
            va + static_cast<uint64_t>(slot) * wqeSize, readBytes, channel);
    }
}

inline void SampleCqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t cqeSize, uint32_t tail,
    HcommDfxChannelInfo &channel, void *reader)
{
    const uint32_t leading = std::min<uint32_t>(depth, QUEUE_LEADING_WINDOW);
    for (uint32_t idx = 0; idx < leading; ++idx) {
        (void)AppendQueueEntry(reader, HCOMM_DFX_ENTRY_URMA_CQE, queueIndex, idx, idx,
            va + static_cast<uint64_t>(idx) * cqeSize, cqeSize, channel);
    }
    const uint32_t window = tail < (QUEUE_TAIL_WINDOW - 1U) ? tail + 1U : QUEUE_TAIL_WINDOW;
    const uint32_t start = tail >= window ? tail - window + 1U : 0U;
    for (uint32_t idx = 0; idx < window; ++idx) {
        const uint32_t cursor = start + idx;
        const uint32_t slot = cursor % depth;
        (void)AppendQueueEntry(reader, HCOMM_DFX_ENTRY_URMA_CQE, queueIndex, slot, cursor,
            va + static_cast<uint64_t>(slot) * cqeSize, cqeSize, channel);
    }
}

inline void CollectSqContexts(uint64_t addr, uint32_t sqNum, HcommDfxChannelInfo &channel, void *reader)
{
    channel.sqContextDumpNum = 0;
    if (addr == 0) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(sqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        SqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(SqContext), ctx, "SqContext")) {
            continue;
        }
        HcommDfxQueueCtxDump &out = channel.sqContexts[channel.sqContextDumpNum++];
        out.rawSize = std::min<uint32_t>(sizeof(SqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != SQ_CONTEXT_TYPE_UB_JFS) {
            continue;
        }
        const auto &jfs = ctx.contextInfo.ubJfs;
        out.headValid = CopyFromDevice(jfs.headAddr, out.head, "SqHead") ? 1U : 0U;
        out.tailValid = CopyFromDevice(jfs.tailAddr, out.tail, "SqTail") ? 1U : 0U;
        if (jfs.sqVa == 0 || jfs.sqDepth == 0 || jfs.wqeSize == 0) {
            continue;
        }
        SampleSqEntries(idx, jfs.sqVa, jfs.sqDepth, jfs.wqeSize, out.head, channel, reader);
    }
}

inline void CollectCqContexts(uint64_t addr, uint32_t cqNum, HcommDfxChannelInfo &channel, void *reader)
{
    channel.cqContextDumpNum = 0;
    if (addr == 0) {
        return;
    }
    const uint32_t limit = std::min<uint32_t>(cqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        CqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(CqContext), ctx, "CqContext")) {
            continue;
        }
        HcommDfxQueueCtxDump &out = channel.cqContexts[channel.cqContextDumpNum++];
        out.rawSize = std::min<uint32_t>(sizeof(CqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != CQ_CONTEXT_TYPE_UB_JFC) {
            continue;
        }
        const auto &jfc = ctx.contextInfo.ubJfc;
        out.headValid = CopyFromDevice(jfc.headAddr, out.head, "CqHead") ? 1U : 0U;
        out.tailValid = CopyFromDevice(jfc.tailAddr, out.tail, "CqTail") ? 1U : 0U;
        if (jfc.scqVa == 0 || jfc.cqDepth == 0 || jfc.cqeSize == 0) {
            continue;
        }
        SampleCqEntries(idx, jfc.scqVa, jfc.cqDepth, jfc.cqeSize, out.tail, channel, reader);
    }
}

inline bool CollectChannel(ChannelHandle channelHandle, HcommDfxChannelInfo &channel, void *reader)
{
    std::memset(&channel, 0, sizeof(channel));
    channel.handle = channelHandle;
    if (channelHandle == 0) {
        return false;
    }

    ChannelEntity entity;
    if (!CopyFromDevice(channelHandle, entity, "ChannelEntity")) {
        return false;
    }
    channel.valid = 1U;
    channel.channelEntitySize = std::min<uint32_t>(sizeof(ChannelEntity), HCOMM_DFX_CHANNEL_ENTITY_BYTES);
    std::memcpy(channel.channelEntity, &entity, channel.channelEntitySize);

    CollectBuffers(PtrToU64(entity.localBufferAddr), entity.localBufferNum, channel.localBuffers,
        channel.localBufferDumpNum);
    CollectBuffers(PtrToU64(entity.remoteBufferAddr), entity.remoteBufferNum, channel.remoteBuffers,
        channel.remoteBufferDumpNum);
    CollectSqContexts(PtrToU64(entity.sqContextAddr), entity.sqNum, channel, reader);
    CollectCqContexts(PtrToU64(entity.cqContextAddr), entity.cqNum, channel, reader);
    return true;
}

inline int32_t GetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len)
{
    if (len == nullptr || (channelNum != 0 && channels == nullptr)) {
        return HCOMM_FAILED;
    }
    *len = sizeof(HcommDfxChannelDumpData);
    return HCOMM_SUCCESS;
}

inline int32_t DumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len)
{
    if (channelNum == 0 || channels == nullptr || buffer == nullptr || len < sizeof(HcommDfxChannelDumpData)) {
        return HCOMM_FAILED;
    }

    auto *dump = reinterpret_cast<HcommDfxChannelDumpData *>(buffer);
    std::memset(dump, 0, sizeof(HcommDfxChannelDumpData));
    dump->magic = HCOMM_DFX_CHANNEL_DUMP_MAGIC;
    dump->version = HCOMM_DFX_CHANNEL_DUMP_VERSION;
    dump->size = sizeof(HcommDfxChannelDumpData);
    dump->channelNum = std::min<uint32_t>(channelNum, HCOMM_DFX_MAX_CHANNELS);

    auto *reader = CreateQueueReader();
    if (reader == nullptr) {
        return HCOMM_FAILED;
    }

    int32_t ret = HCOMM_SUCCESS;
    for (uint32_t idx = 0; idx < dump->channelNum; ++idx) {
        if (!CollectChannel(channels[idx], dump->channels[idx], reader)) {
            ret = HCOMM_FAILED;
        }
    }
    DestroyQueueReader(reader);
    (void)ParseAndPrintChannelDump(*dump);
    return ret;
}

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_COLLECT_H
