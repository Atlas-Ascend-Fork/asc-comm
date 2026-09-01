/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_dfx_impl.h"

#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "../../common/hcomm_inner_def.h"
#undef HCOMM_INCLUDE_INTERNAL_HEADERS

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace AscendC {
namespace HcommDfx {
namespace {
constexpr uint32_t QUEUE_LEADING_WINDOW = 4U;
constexpr uint32_t QUEUE_TAIL_WINDOW = 4U;

// Reads queue entries (WQE/CQE) that live in device-side queue regions. These VAs
// are not plain D2H-readable, so a helper AIV kernel copies them out for us.
class QueueReader {
public:
    QueueReader() : reader_(CreateQueueReader())
    {}

    QueueReader(const QueueReader &) = delete;
    QueueReader &operator=(const QueueReader &) = delete;

    ~QueueReader()
    {
        DestroyQueueReader(reader_);
    }

    bool Read(uint64_t addr, uint8_t *data, uint32_t size)
    {
        if (reader_ == nullptr || addr == 0 || data == nullptr || size == 0 || size > HCOMM_DFX_QUEUE_ENTRY_BYTES) {
            return false;
        }
        return ReadQueueEntry(reader_, addr, data, size);
    }

private:
    void *reader_ = nullptr;
};

// Copy sizeof(T) bytes from a device VA into a host value. The output is always
// zero-filled first, so callers observe a defined value even when addr is null or
// the copy fails.
template <typename T>
bool CopyFromDevice(uint64_t addr, T &value, const char *name)
{
    std::memset(&value, 0, sizeof(T));
    if (addr == 0) {
        return false;
    }
    aclError ret = aclrtMemcpy(&value, sizeof(T), reinterpret_cast<const void *>(addr), sizeof(T),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        Trace("copy %s failed addr=0x%llx size=%llu ret=%d", name, static_cast<unsigned long long>(addr),
            static_cast<unsigned long long>(sizeof(T)), ret);
        return false;
    }
    return true;
}

uint64_t PtrToU64(const void *ptr)
{
    return reinterpret_cast<uint64_t>(ptr);
}

// Record one sampled queue entry: dedup against already-collected slots, then read
// its raw bytes via the helper kernel and tag it with a read status. Returns whether
// the entry's payload is available.
bool AppendQueueEntry(HcommDfxChannelInfo &channel, QueueReader &reader, uint32_t entryType, uint32_t queueIndex,
    uint32_t entryIndex, uint32_t cursor, uint64_t addr, uint32_t size)
{
    if (channel.queueEntryDumpNum >= HCOMM_DFX_MAX_QUEUE_ENTRIES || addr == 0 || size == 0) {
        return false;
    }
    for (uint32_t idx = 0; idx < channel.queueEntryDumpNum; ++idx) {
        const HcommDfxQueueEntry &entry = channel.queueEntries[idx];
        if (entry.entryType == entryType && entry.queueIndex == queueIndex && entry.entryIndex == entryIndex &&
            entry.addr == addr) {
            return entry.readStatus == HCOMM_DFX_READ_OK;
        }
    }
    HcommDfxQueueEntry &entry = channel.queueEntries[channel.queueEntryDumpNum++];
    entry.entryType = entryType;
    entry.queueIndex = queueIndex;
    entry.entryIndex = entryIndex;
    entry.cursor = cursor;
    entry.addr = addr;
    entry.size = std::min<uint32_t>(size, HCOMM_DFX_QUEUE_ENTRY_BYTES);
    entry.readStatus = reader.Read(addr, entry.data, entry.size) ? HCOMM_DFX_READ_OK : HCOMM_DFX_READ_LAUNCH_FAIL;
    return entry.readStatus == HCOMM_DFX_READ_OK;
}

// A URMA WQE occupies at least two 32B basic-blocks; when the descriptor is small
// enough, sample two blocks so an inline payload following it is captured too. The
// result is clamped to the entry buffer size.
uint32_t SqWqeReadBytes(uint32_t wqeSize)
{
    uint32_t bytes = wqeSize <= HCOMM_DFX_QUEUE_ENTRY_BYTES / 2U ? wqeSize * 2U : wqeSize;
    return std::min<uint32_t>(bytes, HCOMM_DFX_QUEUE_ENTRY_BYTES);
}

// Sample the leading WQEs plus a short window around the current SQ head.
void SampleSqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t wqeSize, uint32_t head,
    HcommDfxChannelInfo &channel, QueueReader &reader)
{
    const uint32_t readBytes = SqWqeReadBytes(wqeSize);
    const uint32_t leading = std::min<uint32_t>(depth, QUEUE_LEADING_WINDOW);
    for (uint32_t idx = 0; idx < leading; ++idx) {
        (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_WQE, queueIndex, idx, idx,
            va + static_cast<uint64_t>(idx) * wqeSize, readBytes);
    }
    const uint32_t window = head == 0 ? 1U : std::min<uint32_t>(head, QUEUE_TAIL_WINDOW);
    const uint32_t start = head >= window ? head - window : 0U;
    for (uint32_t idx = 0; idx < window; ++idx) {
        const uint32_t slot = (start + idx) % depth;
        (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_WQE, queueIndex, slot, start + idx,
            va + static_cast<uint64_t>(slot) * wqeSize, readBytes);
    }
}

// Sample the leading CQEs plus a short window ending at the current CQ tail.
void SampleCqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t cqeSize, uint32_t tail,
    HcommDfxChannelInfo &channel, QueueReader &reader)
{
    const uint32_t leading = std::min<uint32_t>(depth, QUEUE_LEADING_WINDOW);
    for (uint32_t idx = 0; idx < leading; ++idx) {
        (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_CQE, queueIndex, idx, idx,
            va + static_cast<uint64_t>(idx) * cqeSize, cqeSize);
    }
    const uint32_t window = tail < (QUEUE_TAIL_WINDOW - 1U) ? tail + 1U : QUEUE_TAIL_WINDOW;
    const uint32_t start = tail >= window ? tail - window + 1U : 0U;
    for (uint32_t idx = 0; idx < window; ++idx) {
        const uint32_t cursor = start + idx;
        const uint32_t slot = cursor % depth;
        (void)AppendQueueEntry(channel, reader, HCOMM_DFX_ENTRY_URMA_CQE, queueIndex, slot, cursor,
            va + static_cast<uint64_t>(slot) * cqeSize, cqeSize);
    }
}

void CollectBuffers(uint64_t addr, uint32_t num, HcommDfxBufferDump *buffers, uint32_t &dumpNum)
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

void CollectSqContexts(uint64_t addr, uint32_t sqNum, HcommDfxChannelInfo &channel, QueueReader &reader)
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

void CollectCqContexts(uint64_t addr, uint32_t cqNum, HcommDfxChannelInfo &channel, QueueReader &reader)
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

bool CollectChannel(ChannelHandle channelHandle, HcommDfxChannelInfo &channel, QueueReader &reader)
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
} // namespace

int32_t GetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len)
{
    if (len == nullptr || (channelNum != 0 && channels == nullptr)) {
        return HCOMM_FAILED;
    }
    *len = sizeof(HcommDfxChannelDumpData);
    return HCOMM_SUCCESS;
}

int32_t DumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len)
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

    QueueReader reader;
    int32_t ret = HCOMM_SUCCESS;
    for (uint32_t idx = 0; idx < dump->channelNum; ++idx) {
        if (!CollectChannel(channels[idx], dump->channels[idx], reader)) {
            ret = HCOMM_FAILED;
        }
    }
    (void)ParseAndPrintChannelDump(*dump);
    return ret;
}

} // namespace HcommDfx
} // namespace AscendC
