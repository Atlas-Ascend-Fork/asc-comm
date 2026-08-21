/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/dfx/hcomm_channel_dump.h"
#include "hcomm_dfx_channel_dump_impl.h"

#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "../common/hcomm_inner_def.h"
#undef HCOMM_INCLUDE_INTERNAL_HEADERS

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" aclError aclrtGetFuncBySymbol(const void *symbol, aclrtFuncHandle *funcHandle);
extern "C" aclError aclrtFunctionGetBinary(aclrtFuncHandle funcHandle, aclrtBinHandle *binHandle);

namespace AscendC {
namespace HcommDfx {
namespace {
constexpr uint32_t QUEUE_LEADING_WINDOW = 4U;
constexpr uint32_t QUEUE_TAIL_WINDOW = 4U;

struct ExceptionRecord {
    std::atomic<bool> pending{false};
    std::atomic<uint64_t> count{0};
    uint32_t taskId = 0;
    uint32_t streamId = 0;
    uint32_t deviceId = 0;
};

ExceptionRecord g_exception;

bool IsTraceEnabled()
{
    const char *enabled = std::getenv("HCOMM_DFX_DUMP_TRACE");
    return enabled != nullptr && enabled[0] != '\0' && enabled[0] != '0';
}

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

        // URMA-only: WQEs and the live head/tail counters are sampled from the UB JFS arm.
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

        // URMA-only: CQEs and the live head/tail counters are sampled from the UB JFC arm.
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

// ---- Parse / print side: reinterpret the raw blocks against the hcomm structs ----

void PrintBuffer(const char *tag, uint32_t idx, const HcommDfxBufferDump &dump)
{
    if (dump.rawSize < sizeof(RegedBufferEntity)) {
        std::printf("[HcommDfxDump][parsed] %s[%u] rawSize=%u (truncated)\n", tag, idx, dump.rawSize);
        return;
    }
    RegedBufferEntity buffer;
    std::memcpy(&buffer, dump.raw, sizeof(buffer));
    const uint64_t bufAddr = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.addr : buffer.bufferInfo.rma.addr;
    const uint64_t bufSize = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.size : buffer.bufferInfo.rma.size;
    std::printf("[HcommDfxDump][parsed] %s[%u] type=%d addr=0x%llx size=%llu\n", tag, idx,
        static_cast<int32_t>(buffer.type), static_cast<unsigned long long>(bufAddr),
        static_cast<unsigned long long>(bufSize));
}

void PrintSqContext(uint32_t idx, const HcommDfxQueueCtxDump &dump)
{
    SqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == SQ_CONTEXT_TYPE_UB_JFS) {
        const auto &jfs = ctx.contextInfo.ubJfs;
        std::printf("[HcommDfxDump][parsed] SqContext[%u] type=UB_JFS jfsID=%u sqVa=0x%llx depth=%u wqeSize=%u "
                    "dbVa=0x%llx head=%u(valid=%u) tail=%u(valid=%u)\n",
            idx, jfs.jfsID, static_cast<unsigned long long>(jfs.sqVa), jfs.sqDepth, jfs.wqeSize,
            static_cast<unsigned long long>(jfs.dbVa), dump.head, dump.headValid, dump.tail, dump.tailValid);
    } else {
        std::printf("[HcommDfxDump][parsed] SqContext[%u] type=%d (non-URMA, raw only) "
                    "head=%u(valid=%u) tail=%u(valid=%u)\n",
            idx, static_cast<int32_t>(ctx.type), dump.head, dump.headValid, dump.tail, dump.tailValid);
    }
}

void PrintCqContext(uint32_t idx, const HcommDfxQueueCtxDump &dump)
{
    CqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == CQ_CONTEXT_TYPE_UB_JFC) {
        const auto &jfc = ctx.contextInfo.ubJfc;
        std::printf("[HcommDfxDump][parsed] CqContext[%u] type=UB_JFC jfcID=%u cqVa=0x%llx depth=%u cqeSize=%u "
                    "dbVa=0x%llx head=%u(valid=%u) tail=%u(valid=%u)\n",
            idx, jfc.jfcID, static_cast<unsigned long long>(jfc.scqVa), jfc.cqDepth, jfc.cqeSize,
            static_cast<unsigned long long>(jfc.dbVa), dump.head, dump.headValid, dump.tail, dump.tailValid);
    } else {
        std::printf("[HcommDfxDump][parsed] CqContext[%u] type=%d (non-URMA, raw only) "
                    "head=%u(valid=%u) tail=%u(valid=%u)\n",
            idx, static_cast<int32_t>(ctx.type), dump.head, dump.headValid, dump.tail, dump.tailValid);
    }
}

void PrintWqe(const HcommDfxQueueEntry &entry)
{
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        std::printf("[HcommDfxDump][parsed] Hcomm URMA WQE queue=%u index=%u cursor=%u addr=0x%llx readStatus=%u "
                    "(no payload)\n",
            entry.queueIndex, entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr),
            entry.readStatus);
        return;
    }
    HcommUrmaSqeCtx wqe;
    std::memset(&wqe, 0, sizeof(wqe));
    std::memcpy(&wqe, entry.data, std::min<uint32_t>(entry.size, sizeof(wqe)));
    std::printf("[HcommDfxDump][parsed] Hcomm URMA WQE queue=%u index=%u cursor=%u addr=0x%llx size=%u opcode=0x%x "
                "sqeBbIdx=%u tpId=0x%x sgeNum=%u inlineMsgLen=%u rmtJettyOrSegId=0x%x rmtEid=0x%llx%016llx\n",
        entry.queueIndex, entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr), entry.size,
        wqe.opcode, wqe.sqeBbIdx, wqe.tpId, wqe.sgeNum, wqe.inlineMsgLen, wqe.rmtJettyOrSegId,
        static_cast<unsigned long long>(wqe.rmtEidH), static_cast<unsigned long long>(wqe.rmtEidL));
}

void PrintCqe(const HcommDfxQueueEntry &entry)
{
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        std::printf("[HcommDfxDump][parsed] Hcomm URMA CQE queue=%u index=%u cursor=%u addr=0x%llx readStatus=%u "
                    "(no payload)\n",
            entry.queueIndex, entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr),
            entry.readStatus);
        return;
    }
    HcommUrmaJfcCqeCtx cqe;
    std::memset(&cqe, 0, sizeof(cqe));
    std::memcpy(&cqe, entry.data, std::min<uint32_t>(entry.size, sizeof(cqe)));
    std::printf("[HcommDfxDump][parsed] Hcomm URMA CQE queue=%u index=%u cursor=%u addr=0x%llx size=%u status=%u "
                "substatus=%u opcode=%u sR=%u entryIdx=%u byteCnt=%u tpn=0x%x\n",
        entry.queueIndex, entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr), entry.size,
        cqe.status, cqe.substatus, cqe.opcode, cqe.sR, cqe.entryIdx, cqe.byteCnt, cqe.tpn);
}

void PrintChannel(uint32_t channelIdx, const HcommDfxChannelInfo &channel)
{
    ChannelEntity entity;
    std::memset(&entity, 0, sizeof(entity));
    if (channel.valid != 0 && channel.channelEntitySize > 0U) {
        std::memcpy(&entity, channel.channelEntity, std::min<uint32_t>(channel.channelEntitySize, sizeof(entity)));
    }
    std::printf("[HcommDfxDump][parsed] Channel[%u] handle=0x%llx valid=%u engine=%d protocol=%d "
                "localBufferNum=%u remoteBufferNum=%u sqNum=%u cqNum=%u sqHead=%u sqTail=%u cqHead=%u cqTail=%u\n",
        channelIdx, static_cast<unsigned long long>(channel.handle), channel.valid,
        static_cast<int32_t>(entity.engine), static_cast<int32_t>(entity.protocol), entity.localBufferNum,
        entity.remoteBufferNum, entity.sqNum, entity.cqNum, entity.sqHead, entity.sqTail, entity.cqHead,
        entity.cqTail);

    const uint32_t localNum = std::min<uint32_t>(channel.localBufferDumpNum, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < localNum; ++idx) {
        PrintBuffer("localBuffer", idx, channel.localBuffers[idx]);
    }
    const uint32_t remoteNum = std::min<uint32_t>(channel.remoteBufferDumpNum, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < remoteNum; ++idx) {
        PrintBuffer("remoteBuffer", idx, channel.remoteBuffers[idx]);
    }
    const uint32_t sqNum = std::min<uint32_t>(channel.sqContextDumpNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < sqNum; ++idx) {
        PrintSqContext(idx, channel.sqContexts[idx]);
    }
    const uint32_t cqNum = std::min<uint32_t>(channel.cqContextDumpNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < cqNum; ++idx) {
        PrintCqContext(idx, channel.cqContexts[idx]);
    }
    const uint32_t entryNum = std::min<uint32_t>(channel.queueEntryDumpNum, HCOMM_DFX_MAX_QUEUE_ENTRIES);
    for (uint32_t idx = 0; idx < entryNum; ++idx) {
        const HcommDfxQueueEntry &entry = channel.queueEntries[idx];
        if (entry.entryType == HCOMM_DFX_ENTRY_URMA_WQE) {
            PrintWqe(entry);
        } else {
            PrintCqe(entry);
        }
    }
}

int32_t ParseAndPrintChannelDump(const HcommDfxChannelDumpData &dump)
{
    if (dump.magic != HCOMM_DFX_CHANNEL_DUMP_MAGIC || dump.size < sizeof(HcommDfxChannelDumpData)) {
        std::printf("[HcommDfxDump][parsed] invalid dump magic=0x%x size=%u\n", dump.magic, dump.size);
        return HCOMM_FAILED;
    }
    if (dump.version != HCOMM_DFX_CHANNEL_DUMP_VERSION) {
        std::printf("[HcommDfxDump][parsed] version mismatch dump=%u expect=%u, continuing best-effort\n",
            dump.version, HCOMM_DFX_CHANNEL_DUMP_VERSION);
    }
    const uint32_t channelNum = std::min<uint32_t>(dump.channelNum, HCOMM_DFX_MAX_CHANNELS);
    for (uint32_t channelIdx = 0; channelIdx < channelNum; ++channelIdx) {
        PrintChannel(channelIdx, dump.channels[channelIdx]);
    }
    return HCOMM_SUCCESS;
}

void RecordException(aclrtExceptionInfo *exceptionInfo)
{
    if (exceptionInfo == nullptr) {
        return;
    }

    g_exception.taskId = aclrtGetTaskIdFromExceptionInfo(exceptionInfo);
    g_exception.streamId = aclrtGetStreamIdFromExceptionInfo(exceptionInfo);
    g_exception.deviceId = aclrtGetDeviceIdFromExceptionInfo(exceptionInfo);
    uint64_t count = g_exception.count.fetch_add(1U) + 1U;
    g_exception.pending.store(true, std::memory_order_release);
    std::printf("[HcommDfxDump] exception callback entered count=%llu task=%u stream=%u device=%u\n",
        static_cast<unsigned long long>(count), g_exception.taskId, g_exception.streamId, g_exception.deviceId);
    std::fflush(stdout);
}

void ExceptionCallback(aclrtExceptionInfo *exceptionInfo, void *userData)
{
    (void)userData;
    RecordException(exceptionInfo);
}
} // namespace

void Trace(const char *format, ...)
{
    if (!IsTraceEnabled()) {
        return;
    }
    std::fprintf(stderr, "[HcommDfxDump][TRACE] ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

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

bool HasPendingException()
{
    return g_exception.pending.load(std::memory_order_acquire);
}

void ClearPendingException()
{
    g_exception.pending.store(false, std::memory_order_release);
    g_exception.count.store(0U, std::memory_order_release);
}

} // namespace HcommDfx

// 公开接口（声明见 include/aicore/hcomm/dfx/hcomm_channel_dump.h）。
// 其余采集/判定逻辑收敛在内部 namespace HcommDfx，不对外暴露。
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
{
    if (kernelSymbol == nullptr) {
        return HCOMM_FAILED;
    }

    aclrtFuncHandle funcHandle = nullptr;
    aclrtBinHandle binHandle = nullptr;
    if (aclrtGetFuncBySymbol(kernelSymbol, &funcHandle) != ACL_SUCCESS ||
        aclrtFunctionGetBinary(funcHandle, &binHandle) != ACL_SUCCESS ||
        aclrtBinarySetExceptionCallback(binHandle, HcommDfx::ExceptionCallback, nullptr) != ACL_SUCCESS) {
        std::printf("[HcommDfxDump] bind exception callback failed\n");
        std::fflush(stdout);
        return HCOMM_FAILED;
    }

    HcommDfx::g_exception.pending.store(false, std::memory_order_release);
    HcommDfx::g_exception.count.store(0U, std::memory_order_release);
    std::printf("[HcommDfxDump] exception callback registered\n");
    std::fflush(stdout);
    return HCOMM_SUCCESS;
}

int32_t HcommChannelInfoDump(ChannelHandle channel)
{
    if (!HcommDfx::HasPendingException()) {
        return HCOMM_SUCCESS;
    }

    uint32_t dumpLen = 0;
    int32_t ret = HcommDfx::GetChannelDumpInfoSize(1U, &channel, &dumpLen);
    if (ret != HCOMM_SUCCESS || dumpLen == 0) {
        return ret;
    }

    void *dumpBuffer = std::malloc(dumpLen);
    if (dumpBuffer == nullptr) {
        return HCOMM_FAILED;
    }
    std::memset(dumpBuffer, 0, dumpLen);
    ret = HcommDfx::DumpChannelInfo(1U, &channel, dumpBuffer, dumpLen);
    std::free(dumpBuffer);
    std::printf("[HcommDfxDump] HcommChannelInfoDump ret=%d len=%u\n", ret, dumpLen);
    std::fflush(stdout);
    return ret;
}

} // namespace AscendC
