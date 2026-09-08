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

    Trace("queue read launch addr=0x%llx size=%u stream=%p", static_cast<unsigned long long>(addr), size, ctx->stream);
    HcommDfxQueueReadArgs args = {addr, reinterpret_cast<uint64_t>(ctx->deviceResult), size, 0};
    hcomm_dfx_queue_read_kernel<<<1, 0, ctx->stream>>>(args);
    aclError ret = aclrtSynchronizeStream(ctx->stream);
    Trace("queue read sync done addr=0x%llx size=%u ret=%d", static_cast<unsigned long long>(addr), size, ret);

    HcommDfxQueueReadResult result;
    std::memset(&result, 0, sizeof(result));
    if (ret == ACL_SUCCESS) {
        ret = aclrtMemcpy(&result, sizeof(result), ctx->deviceResult, sizeof(result), ACL_MEMCPY_DEVICE_TO_HOST);
        Trace("queue read memcpy done addr=0x%llx size=%u ret=%d resultRet=%u resultSize=%u",
              static_cast<unsigned long long>(addr), size, ret, result.ret, result.size);
    }

    if (ret != ACL_SUCCESS || result.ret != HCOMM_DFX_QUEUE_READ_SUCCESS || result.size != size) {
        Trace("queue read failed addr=0x%llx ret=%d entryRet=%u size=%u expect=%u",
            static_cast<unsigned long long>(addr), ret, result.ret, result.size, size);
        return false;
    }

    std::memcpy(data, result.data, size);
    return true;
}

inline bool ReadQueueEntryWrapped(void *reader, uint64_t baseAddr, uint32_t depth, uint32_t entrySize,
                                  uint32_t slot, uint32_t size, uint8_t *data, const char *tag,
                                  uint32_t queueIndex, uint32_t cursor)
{
    if (reader == nullptr || baseAddr == 0U || data == nullptr || depth == 0U || entrySize == 0U ||
        slot >= depth || size == 0U) {
        Trace("%s wrapped read invalid args queue=%u cursor=%u base=0x%llx slot=%u depth=%u entrySize=%u size=%u",
              tag, queueIndex, cursor, static_cast<unsigned long long>(baseAddr), slot, depth, entrySize, size);
        return false;
    }

    const uint64_t queueBytes = static_cast<uint64_t>(depth) * static_cast<uint64_t>(entrySize);
    const uint64_t offset = static_cast<uint64_t>(slot) * static_cast<uint64_t>(entrySize);
    if (offset >= queueBytes) {
        Trace("%s wrapped read offset out of range queue=%u cursor=%u base=0x%llx slot=%u depth=%u entrySize=%u "
              "size=%u offset=%llu queueBytes=%llu",
              tag, queueIndex, cursor, static_cast<unsigned long long>(baseAddr), slot, depth, entrySize, size,
              static_cast<unsigned long long>(offset), static_cast<unsigned long long>(queueBytes));
        return false;
    }

    const uint32_t totalSize = std::min<uint32_t>(
        size, std::min<uint64_t>(queueBytes, HCOMM_DFX_QUEUE_ENTRY_BYTES));
    const uint32_t firstSize = static_cast<uint32_t>(std::min<uint64_t>(totalSize, queueBytes - offset));
    if (firstSize > 0U && !ReadQueueEntry(reader, baseAddr + offset, data, firstSize)) {
        Trace("%s wrapped read first segment failed queue=%u cursor=%u base=0x%llx slot=%u depth=%u entrySize=%u "
              "size=%u firstSize=%u",
              tag, queueIndex, cursor, static_cast<unsigned long long>(baseAddr), slot, depth, entrySize, size,
              firstSize);
        return false;
    }

    const uint32_t secondSize = totalSize - firstSize;
    if (secondSize > 0U && !ReadQueueEntry(reader, baseAddr, data + firstSize, secondSize)) {
        Trace("%s wrapped read second segment failed queue=%u cursor=%u base=0x%llx slot=%u depth=%u entrySize=%u "
              "size=%u secondSize=%u",
              tag, queueIndex, cursor, static_cast<unsigned long long>(baseAddr), slot, depth, entrySize, size,
              secondSize);
        return false;
    }

    return true;
}

inline bool AppendQueueEntry(void *reader, uint32_t entryType, uint32_t queueIndex, uint32_t entryIndex,
                             uint32_t cursor, uint64_t addr, uint32_t size, HcommDfxChannelInfo &out)
{
    if (out.queueEntryDumpNum >= HCOMM_DFX_MAX_QUEUE_ENTRIES || addr == 0 || size == 0) {
        Trace("append queue entry invalid args type=%u queue=%u index=%u cursor=%u addr=0x%llx size=%u "
              "dumpNum=%u",
              entryType, queueIndex, entryIndex, cursor, static_cast<unsigned long long>(addr), size,
              out.queueEntryDumpNum);
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
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        Trace("append queue entry read failed type=%u queue=%u index=%u cursor=%u addr=0x%llx size=%u readStatus=%u",
              entryType, queueIndex, entryIndex, cursor, static_cast<unsigned long long>(addr), entry.size,
              entry.readStatus);
    }
    return entry.readStatus == HCOMM_DFX_READ_OK;
}

inline bool AppendQueueEntryWrapped(void *reader, uint32_t entryType, uint32_t queueIndex, uint32_t entryIndex,
                                    uint32_t cursor, uint64_t baseAddr, uint32_t depth, uint32_t entrySize,
                                    uint32_t size, HcommDfxChannelInfo &out)
{
    if (out.queueEntryDumpNum >= HCOMM_DFX_MAX_QUEUE_ENTRIES || baseAddr == 0 || depth == 0U || entrySize == 0U ||
        size == 0U || entryIndex >= depth) {
        Trace("append wrapped queue entry invalid args type=%u queue=%u index=%u cursor=%u base=0x%llx depth=%u "
              "entrySize=%u size=%u dumpNum=%u",
              entryType, queueIndex, entryIndex, cursor, static_cast<unsigned long long>(baseAddr), depth,
              entrySize, size, out.queueEntryDumpNum);
        return false;
    }

    uint64_t addr = baseAddr + static_cast<uint64_t>(entryIndex) * static_cast<uint64_t>(entrySize);
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
    entry.readStatus = ReadQueueEntryWrapped(reader, baseAddr, depth, entrySize, entryIndex, entry.size,
                                             entry.data, entryType == HCOMM_DFX_ENTRY_URMA_WQE ? "sq" : "cq",
                                             queueIndex, cursor) ? HCOMM_DFX_READ_OK : HCOMM_DFX_READ_LAUNCH_FAIL;
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        Trace("append wrapped queue entry read failed type=%u queue=%u index=%u cursor=%u base=0x%llx depth=%u "
              "entrySize=%u size=%u readStatus=%u",
              entryType, queueIndex, entryIndex, cursor, static_cast<unsigned long long>(baseAddr), depth,
              entrySize, entry.size, entry.readStatus);
    }
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

inline bool SampleSqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t wqeSize, uint32_t sqHead,
    uint32_t sqTail, HcommDfxChannelInfo &channel, void *reader)
{
    if (depth == 0U || wqeSize == 0U) {
        Trace("sq sample invalid queue=%u base=0x%llx depth=%u wqeSize=%u head=%u tail=%u", queueIndex,
              static_cast<unsigned long long>(va), depth, wqeSize, sqHead, sqTail);
        return false;
    }
    const uint32_t readBytes = SqWqeReadBytes(wqeSize);
    // Outstanding (submitted but not yet consumed) WQEs occupy [sqTail, sqHead). sqHead/sqTail are free-running BB
    // counters, so the subtraction is wrap-safe; clamp to what the ring and the dump buffer can actually hold.
    const uint32_t outstanding = sqHead - sqTail;
    const uint32_t count = std::min<uint32_t>(outstanding, std::min<uint32_t>(depth, HCOMM_DFX_MAX_QUEUE_ENTRIES));
    for (uint32_t idx = 0; idx < count; ++idx) {
        const uint32_t cursor = sqTail + idx;
        const uint32_t slot = cursor % depth;
        if (!AppendQueueEntryWrapped(reader, HCOMM_DFX_ENTRY_URMA_WQE, queueIndex, slot, cursor,
                                     va, depth, wqeSize, readBytes, channel)) {
            Trace("sq sample stop queue=%u slot=%u cursor=%u base=0x%llx depth=%u wqeSize=%u readBytes=%u",
                  queueIndex, slot, cursor, static_cast<unsigned long long>(va), depth, wqeSize, readBytes);
            return false;
        }
    }
    return true;
}

inline bool SampleCqEntries(uint32_t queueIndex, uint64_t va, uint32_t depth, uint32_t cqeSize, uint32_t cqHead,
    uint32_t cqTail, HcommDfxChannelInfo &channel, void *reader)
{
    if (depth == 0U || cqeSize == 0U) {
        Trace("cq sample invalid queue=%u base=0x%llx depth=%u cqeSize=%u head=%u tail=%u", queueIndex,
              static_cast<unsigned long long>(va), depth, cqeSize, cqHead, cqTail);
        return false;
    }
    // Produced-but-not-consumed CQEs occupy [cqTail, cqHead): cqHead is the expected CQE count, cqTail the consumed
    // count. Wrap-safe subtraction, clamped to ring depth and dump capacity.
    const uint32_t outstanding = cqHead - cqTail;
    const uint32_t count = std::min<uint32_t>(outstanding, std::min<uint32_t>(depth, HCOMM_DFX_MAX_QUEUE_ENTRIES));
    for (uint32_t idx = 0; idx < count; ++idx) {
        const uint32_t cursor = cqTail + idx;
        const uint32_t slot = cursor % depth;
        if (!AppendQueueEntryWrapped(reader, HCOMM_DFX_ENTRY_URMA_CQE, queueIndex, slot, cursor,
                                     va, depth, cqeSize, cqeSize, channel)) {
            Trace("cq sample stop queue=%u slot=%u cursor=%u base=0x%llx depth=%u cqeSize=%u",
                  queueIndex, slot, cursor, static_cast<unsigned long long>(va), depth, cqeSize);
            return false;
        }
    }
    return true;
}

inline bool CollectSqContexts(uint64_t addr, uint32_t sqNum, HcommDfxChannelInfo &channel, void *reader,
                              uint32_t sqHead, uint32_t sqTail)
{
    channel.sqContextDumpNum = 0;
    if (addr == 0) {
        Trace("sq context addr is 0, skip collection");
        return true;
    }
    const uint32_t limit = std::min<uint32_t>(sqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        SqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(SqContext), ctx, "SqContext")) {
            Trace("copy SqContext failed queue=%u addr=0x%llx idx=%u", idx, static_cast<unsigned long long>(addr),
                  idx);
            continue;
        }
        if (channel.sqContextDumpNum >= HCOMM_DFX_MAX_QUEUE_CONTEXTS) {
            Trace("sq context dump capacity reached queue=%u addr=0x%llx idx=%u", idx,
                  static_cast<unsigned long long>(addr), idx);
            return false;
        }
        HcommDfxQueueCtxDump &out = channel.sqContexts[channel.sqContextDumpNum++];
        out.rawSize = std::min<uint32_t>(sizeof(SqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != SQ_CONTEXT_TYPE_UB_JFS) {
            continue;
        }
        const auto &jfs = ctx.contextInfo.ubJfs;
        // The cursor source differs by implementation. SIMT keeps an incrementing counter at the
        // u64 *jfs.headAddr (low 32 = curHead, high 32 = wqeCnt) and at the u32 *jfs.tailAddr, and
        // never touches ChannelEntity.sqHead/sqTail (those stay 0). AIV does the reverse: it
        // maintains ChannelEntity.sqHead/sqTail and writes jfs.headAddr/tailAddr as no cursor at all
        // (the spin lock lives in ubJfc.headAddr), so the device read there is not a cursor. A
        // non-zero channel cursor can only come from AIV and is authoritative; when it is 0 (SIMT, or
        // an AIV queue that never advanced) fall back to the device counter, trusting it only when it
        // is non-zero. Both cases mark the value present so parse prints no spurious ERROR line.
        // For SIMT the SQ head is the low 32 bits of the packed u64 (curHead); mirror the CQ side's
        // u64 read below so the value is not split by a 4-byte copy of a possibly-8-byte pointer.
        uint64_t headVal = 0U;
        uint32_t devHead = 0U;
        uint32_t devTail = 0U;
        bool devHeadOk = CopyFromDevice(jfs.headAddr, headVal, "SqHead") && (headVal & 0xFFFFFFFFULL) != 0U;
        bool devTailOk = CopyFromDevice(jfs.tailAddr, devTail, "SqTail") && devTail != 0U;
        if (devHeadOk) {
            devHead = static_cast<uint32_t>(headVal & 0xFFFFFFFFULL);
        }
        out.head = (sqHead != 0U) ? sqHead : (devHeadOk ? devHead : 0U);
        out.tail = (sqTail != 0U) ? sqTail : (devTailOk ? devTail : 0U);
        out.headValid = 1U;
        out.tailValid = 1U;
        if (jfs.sqVa == 0 || jfs.sqDepth == 0 || jfs.wqeSize == 0) {
            continue;
        }
        // Sample with the same resolved cursors shown above: SIMT's channel sqHead/sqTail are never
        // written (stay 0), so sampling on those would miss every WQE. out.head/out.tail already
        // prefer the channel value (AIV) or fall back to the device counter (SIMT), so sampling and
        // display stay consistent.
        if (!SampleSqEntries(idx, jfs.sqVa, jfs.sqDepth, jfs.wqeSize, out.head, out.tail, channel, reader)) {
            Trace("sq sample failed queue=%u sqVa=0x%llx depth=%u wqeSize=%u sqHead=%u sqTail=%u", idx,
                  static_cast<unsigned long long>(jfs.sqVa), jfs.sqDepth, jfs.wqeSize, out.head, out.tail);
            return false;
        }
    }
    return true;
}

inline bool CollectCqContexts(uint64_t addr, uint32_t cqNum, HcommDfxChannelInfo &channel, void *reader,
                              uint64_t sqContextAddr, uint32_t cqHead, uint32_t cqTail)
{
    channel.cqContextDumpNum = 0;
    if (addr == 0) {
        Trace("cq context addr is 0, skip collection");
        return true;
    }
    const uint32_t limit = std::min<uint32_t>(cqNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < limit; ++idx) {
        CqContext ctx;
        if (!CopyFromDevice(addr + static_cast<uint64_t>(idx) * sizeof(CqContext), ctx, "CqContext")) {
            Trace("copy CqContext failed queue=%u addr=0x%llx idx=%u", idx, static_cast<unsigned long long>(addr),
                  idx);
            continue;
        }
        if (channel.cqContextDumpNum >= HCOMM_DFX_MAX_QUEUE_CONTEXTS) {
            Trace("cq context dump capacity reached queue=%u addr=0x%llx idx=%u", idx,
                  static_cast<unsigned long long>(addr), idx);
            return false;
        }
        HcommDfxQueueCtxDump &out = channel.cqContexts[channel.cqContextDumpNum++];
        out.rawSize = std::min<uint32_t>(sizeof(CqContext), HCOMM_DFX_CONTEXT_BYTES);
        std::memcpy(out.raw, &ctx, out.rawSize);
        if (ctx.type != CQ_CONTEXT_TYPE_UB_JFC) {
            continue;
        }
        const auto &jfc = ctx.contextInfo.ubJfc;
        // Cursor source differs by implementation. SIMT keeps its SQ head in the low 32 bits and the
        // produced-CQE count (wqeCnt, which is exactly its CQ head) in the high 32 bits of the same
        // u64 at the paired SQ's *headAddr, and its CQ tail at *jfc.tailAddr; ChannelEntity.cqHead/
        // cqTail stay 0. AIV does the reverse: it maintains cqHead/cqTail, while ubJfc.headAddr is
        // its Lock/Unlock spin address and never a cursor. Prefer the channel cursor when non-zero
        // (AIV, authoritative); otherwise read the paired SQ's headVal for the CQ head and
        // jfc.tailAddr for the tail (SIMT). Mark both present so parse prints no false ERROR line.
        uint32_t devHead = 0U;
        uint32_t devTail = 0U;
        bool devTailOk = CopyFromDevice(jfc.tailAddr, devTail, "CqTail") && devTail != 0U;
        if (cqHead == 0U && sqContextAddr != 0U) {
            SqContext pairedSq;
            if (CopyFromDevice(sqContextAddr + static_cast<uint64_t>(idx) * sizeof(SqContext), pairedSq,
                    "paired SqContext")) {
                uint64_t headVal = 0U;
                if (CopyFromDevice(pairedSq.contextInfo.ubJfs.headAddr, headVal, "SimtCqHead")) {
                    devHead = static_cast<uint32_t>(headVal >> 32U);  // wqeCnt == CQ head
                }
            }
        }
        out.head = (cqHead != 0U) ? cqHead : devHead;
        out.tail = (cqTail != 0U) ? cqTail : (devTailOk ? devTail : 0U);
        out.headValid = 1U;
        out.tailValid = 1U;
        if (jfc.scqVa == 0 || jfc.cqDepth == 0 || jfc.cqeSize == 0) {
            continue;
        }
        // Sample with the same resolved cursors shown above (Simt's channel cqHead/cqTail stay 0).
        if (!SampleCqEntries(idx, jfc.scqVa, jfc.cqDepth, jfc.cqeSize, out.head, out.tail, channel, reader)) {
            Trace("cq sample failed queue=%u cqVa=0x%llx depth=%u cqeSize=%u cqHead=%u cqTail=%u", idx,
                  static_cast<unsigned long long>(jfc.scqVa), jfc.cqDepth, jfc.cqeSize, out.head, out.tail);
            return false;
        }
    }
    return true;
}

inline bool CollectChannel(ChannelHandle channelHandle, HcommDfxChannelInfo &channel, void *reader)
{
    std::memset(&channel, 0, sizeof(channel));
    channel.handle = channelHandle;
    if (channelHandle == 0) {
        Trace("collect channel failed: channel handle is 0");
        return false;
    }

    ChannelEntity entity;
    if (!CopyFromDevice(channelHandle, entity, "ChannelEntity")) {
        Trace("copy ChannelEntity failed channel=0x%llx", static_cast<unsigned long long>(channelHandle));
        return false;
    }
    channel.valid = 1U;
    channel.channelEntitySize = std::min<uint32_t>(sizeof(ChannelEntity), HCOMM_DFX_CHANNEL_ENTITY_BYTES);
    std::memcpy(channel.channelEntity, &entity, channel.channelEntitySize);

    CollectBuffers(PtrToU64(entity.localBufferAddr), entity.localBufferNum, channel.localBuffers,
        channel.localBufferDumpNum);
    CollectBuffers(PtrToU64(entity.remoteBufferAddr), entity.remoteBufferNum, channel.remoteBuffers,
        channel.remoteBufferDumpNum);
    if (!CollectSqContexts(PtrToU64(entity.sqContextAddr), entity.sqNum, channel, reader, entity.sqHead,
                           entity.sqTail)) {
        Trace("collect sq contexts failed channel=0x%llx sqNum=%u sqHead=%u sqTail=%u",
              static_cast<unsigned long long>(channelHandle), entity.sqNum, entity.sqHead, entity.sqTail);
        return false;
    }
    if (!CollectCqContexts(PtrToU64(entity.cqContextAddr), entity.cqNum, channel, reader,
                           PtrToU64(entity.sqContextAddr), entity.cqHead, entity.cqTail)) {
        Trace("collect cq contexts failed channel=0x%llx cqNum=%u cqHead=%u cqTail=%u",
              static_cast<unsigned long long>(channelHandle), entity.cqNum, entity.cqHead, entity.cqTail);
        return false;
    }
    return true;
}

inline int32_t DumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len)
{
    if (channelNum == 0 || channels == nullptr || buffer == nullptr || len < sizeof(HcommDfxChannelDumpData)) {
        Trace("dump channel info invalid args channelNum=%u channels=%p buffer=%p len=%u", channelNum, channels,
              buffer, len);
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
        Trace("create queue reader failed");
        return HCOMM_FAILED;
    }

    int32_t ret = HCOMM_SUCCESS;
    for (uint32_t idx = 0; idx < dump->channelNum; ++idx) {
        if (!CollectChannel(channels[idx], dump->channels[idx], reader)) {
            Trace("collect channel failed at index=%u handle=0x%llx", idx,
                  static_cast<unsigned long long>(channels[idx]));
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
