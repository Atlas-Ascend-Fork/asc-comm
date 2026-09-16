/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_PARSE_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_PARSE_H

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "acl/acl_rt.h"
#include "../../common/hcomm_inner_def.h"
#include "../common/hcomm_dfx_impl.h"

namespace AscendC {
namespace HcommDfx {

// One dump is assembled into a single string and emitted with one write, so concurrent dumps from multiple ranks
// never interleave line-by-line (item: "一次dump打印合成一条").
inline void DfxAppend(std::string &out, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) {
        out.append(line, std::min<size_t>(static_cast<size_t>(n), sizeof(line) - 1U));
    }
}

inline void EnsureDfxLinePrefix(std::string &out)
{
    constexpr const char *prefix = "[HcommDfxDump] ";
    constexpr size_t prefixLen = 15U;
    std::string prefixed;
    prefixed.reserve(out.size() + prefixLen * 16U);
    size_t pos = 0U;
    while (pos < out.size()) {
        const size_t lineEnd = out.find('\n', pos);
        const size_t len = (lineEnd == std::string::npos) ? out.size() - pos : lineEnd - pos;
        if (len != 0U && out.compare(pos, prefixLen, prefix) != 0) {
            prefixed.append(prefix);
        }
        prefixed.append(out, pos, len);
        if (lineEnd == std::string::npos) {
            break;
        }
        prefixed.push_back('\n');
        pos = lineEnd + 1U;
    }
    out.swap(prefixed);
}

inline void AppendBuffer(std::string &out, const char *tag, uint32_t idx, const HcommDfxBufferDump &dump)
{
    if (dump.rawSize < sizeof(RegedBufferEntity)) {
        DfxAppend(out, "%s[%u] rawSize=%u (truncated)\n", tag, idx, dump.rawSize);
        return;
    }
    RegedBufferEntity buffer;
    std::memcpy(&buffer, dump.raw, std::min<uint32_t>(dump.rawSize, static_cast<uint32_t>(sizeof(buffer))));
    const uint64_t bufAddr = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.addr : buffer.bufferInfo.rma.addr;
    const uint64_t bufSize = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.size : buffer.bufferInfo.rma.size;
    DfxAppend(out, "%s[%u] type=%d addr=0x%llx size=%llu\n", tag, idx,
        static_cast<int32_t>(buffer.type), static_cast<unsigned long long>(bufAddr),
        static_cast<unsigned long long>(bufSize));
}

// Emits the head/tail values without a validity suffix; a read failure becomes an explicit ERROR line instead
// (item: "解析失败就报错，去掉valid=1").
inline void AppendQueueCursorErrors(std::string &out, const char *kind, uint32_t idx, const HcommDfxQueueCtxDump &dump)
{
    if (dump.headValid == 0U) {
        DfxAppend(out, "%s[%u] ERROR: head read failed (device pointer unreadable)\n", kind, idx);
    }
    if (dump.tailValid == 0U) {
        DfxAppend(out, "%s[%u] ERROR: tail read failed (device pointer unreadable)\n", kind, idx);
    }
}

inline void AppendSqContext(std::string &out, uint32_t idx, const HcommDfxQueueCtxDump &dump)
{
    SqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == SQ_CONTEXT_TYPE_UB_JFS) {
        const auto &jfs = ctx.contextInfo.ubJfs;
        DfxAppend(out, "SqContext[%u] type=UB_JFS jfsID=%u sqVa=0x%llx depth=%u wqeSize=%u dbVa=0x%llx "
                       "head=%u tail=%u\n",
            idx, jfs.jfsID, static_cast<unsigned long long>(jfs.sqVa), jfs.sqDepth, jfs.wqeSize,
            static_cast<unsigned long long>(jfs.dbVa), dump.head, dump.tail);
    } else {
        DfxAppend(out, "SqContext[%u] type=%d (non-URMA, raw only) head=%u tail=%u\n",
            idx, static_cast<int32_t>(ctx.type), dump.head, dump.tail);
    }
    AppendQueueCursorErrors(out, "SqContext", idx, dump);
}

inline void AppendCqContext(std::string &out, uint32_t idx, const HcommDfxQueueCtxDump &dump)
{
    CqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == CQ_CONTEXT_TYPE_UB_JFC) {
        const auto &jfc = ctx.contextInfo.ubJfc;
        DfxAppend(out, "CqContext[%u] type=UB_JFC jfcID=%u cqVa=0x%llx depth=%u cqeSize=%u dbVa=0x%llx "
                       "head=%u tail=%u\n",
            idx, jfc.jfcID, static_cast<unsigned long long>(jfc.scqVa), jfc.cqDepth, jfc.cqeSize,
            static_cast<unsigned long long>(jfc.dbVa), dump.head, dump.tail);
    } else {
        DfxAppend(out, "CqContext[%u] type=%d (non-URMA, raw only) head=%u tail=%u\n",
            idx, static_cast<int32_t>(ctx.type), dump.head, dump.tail);
    }
    AppendQueueCursorErrors(out, "CqContext", idx, dump);
}

constexpr uint32_t HCOMM_DFX_RAW_WORD_BYTES = 4U;
constexpr uint32_t HCOMM_DFX_RAW_BB_BYTES = 64U;

inline void AppendRawWords(std::string &out, const uint8_t *data, uint32_t size)
{
    const uint32_t wordNum = (size + HCOMM_DFX_RAW_WORD_BYTES - 1U) / HCOMM_DFX_RAW_WORD_BYTES;
    for (uint32_t idx = 0; idx < wordNum; ++idx) {
        if (idx != 0U) {
            DfxAppend(out, ",");
        }
        uint32_t word = 0U;
        const uint32_t offset = idx * HCOMM_DFX_RAW_WORD_BYTES;
        const uint32_t copySize = std::min<uint32_t>(HCOMM_DFX_RAW_WORD_BYTES, size - offset);
        std::memcpy(&word, data + offset, copySize);
        DfxAppend(out, "0x%08x", word);
    }
}

inline void AppendRawQueueEntry(std::string &out, const char *tag, const char *cursorName,
                                const char *tailName, const HcommDfxQueueEntry &entry, bool splitByBb)
{
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        if (tailName != nullptr) {
            DfxAppend(out, "%s[%s=%u %s=%u]: readStatus=%u (addr=0x%llx no payload)\n",
                tag, cursorName, entry.cursor, tailName, entry.cursor, entry.readStatus,
                static_cast<unsigned long long>(entry.addr));
        } else {
            DfxAppend(out, "%s[%s=%u]: readStatus=%u (addr=0x%llx no payload)\n",
                tag, cursorName, entry.cursor, entry.readStatus, static_cast<unsigned long long>(entry.addr));
        }
        return;
    }

    const uint32_t blockSize = splitByBb ? HCOMM_DFX_RAW_BB_BYTES : entry.size;
    for (uint32_t offset = 0U, bb = 0U; offset < entry.size; offset += blockSize, ++bb) {
        const uint32_t curSize = std::min<uint32_t>(blockSize, entry.size - offset);
        if (tailName != nullptr) {
            DfxAppend(out, "%s[%s=%u %s=%u bb=%u addr=0x%llx size=%u]: ",
                tag, cursorName, entry.cursor + bb, tailName, entry.cursor + bb, bb,
                static_cast<unsigned long long>(entry.addr + offset), curSize);
        } else {
            DfxAppend(out, "%s[%s=%u bb=%u addr=0x%llx size=%u]: ",
                tag, cursorName, entry.cursor + bb, bb,
                static_cast<unsigned long long>(entry.addr + offset), curSize);
        }
        AppendRawWords(out, entry.data + offset, curSize);
        DfxAppend(out, "\n");
    }
}

// Compatibility helpers for the old non-group path.
inline void AppendWqe(std::string &out, const HcommDfxQueueEntry &entry)
{
    AppendRawQueueEntry(out, "WQE", "sqHead", nullptr, entry, true);
}

inline void AppendCqe(std::string &out, const HcommDfxQueueEntry &entry)
{
    AppendRawQueueEntry(out, "CQE", "cqHead", "currentTail", entry, true);
}

inline void AppendWqeGroup(std::string &out, const std::vector<const HcommDfxQueueEntry *> &entries)
{
    for (const HcommDfxQueueEntry *entry : entries) {
        AppendWqe(out, *entry);
    }
}

inline void AppendCqeGroup(std::string &out, const std::vector<const HcommDfxQueueEntry *> &entries)
{
    for (const HcommDfxQueueEntry *entry : entries) {
        AppendCqe(out, *entry);
    }
}

inline void AppendChannel(std::string &out, uint32_t channelIdx, const HcommDfxChannelInfo &channel,
                          const std::vector<HcommDfxQueueEntry> &fullEntries)
{
    ChannelEntity entity;
    std::memset(&entity, 0, sizeof(entity));
    if (channel.valid != 0 && channel.channelEntitySize > 0U) {
        std::memcpy(&entity, channel.channelEntity, std::min<uint32_t>(channel.channelEntitySize, sizeof(entity)));
    }
    DfxAppend(out, "Channel[%u] handle=0x%llx valid=%u engine=%d protocol=%d localBufNum=%u "
                   "remoteBufNum=%u sqNum=%u cqNum=%u sqHead=%u sqTail=%u cqHead=%u cqTail=%u\n",
        channelIdx, static_cast<unsigned long long>(channel.handle), channel.valid,
        static_cast<int32_t>(entity.engine), static_cast<int32_t>(entity.protocol), entity.localBufferNum,
        entity.remoteBufferNum, entity.sqNum, entity.cqNum, entity.sqHead, entity.sqTail, entity.cqHead,
        entity.cqTail);

    const uint32_t localNum = std::min<uint32_t>(channel.localBufferDumpNum, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < localNum; ++idx) {
        AppendBuffer(out, "localBuffer", idx, channel.localBuffers[idx]);
    }
    const uint32_t remoteNum = std::min<uint32_t>(channel.remoteBufferDumpNum, HCOMM_DFX_MAX_BUFFERS);
    for (uint32_t idx = 0; idx < remoteNum; ++idx) {
        AppendBuffer(out, "remoteBuffer", idx, channel.remoteBuffers[idx]);
    }
    const uint32_t sqNum = std::min<uint32_t>(channel.sqContextDumpNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < sqNum; ++idx) {
        AppendSqContext(out, idx, channel.sqContexts[idx]);
    }
    const uint32_t cqNum = std::min<uint32_t>(channel.cqContextDumpNum, HCOMM_DFX_MAX_QUEUE_CONTEXTS);
    for (uint32_t idx = 0; idx < cqNum; ++idx) {
        AppendCqContext(out, idx, channel.cqContexts[idx]);
    }
    std::vector<const HcommDfxQueueEntry *> group;
    uint32_t groupType = HCOMM_DFX_ENTRY_URMA_WQE;
    uint32_t groupQueue = 0;
    for (uint32_t idx = 0; idx < static_cast<uint32_t>(fullEntries.size()); ++idx) {
        const HcommDfxQueueEntry *entry = &fullEntries[idx];
        if (!group.empty() && (entry->entryType != groupType || entry->queueIndex != groupQueue)) {
            if (groupType == HCOMM_DFX_ENTRY_URMA_WQE) AppendWqeGroup(out, group);
            else AppendCqeGroup(out, group);
            group.clear();
        }
        if (group.empty()) {
            groupType = entry->entryType;
            groupQueue = entry->queueIndex;
        }
        group.push_back(entry);
    }
    if (groupType == HCOMM_DFX_ENTRY_URMA_WQE) AppendWqeGroup(out, group);
    else AppendCqeGroup(out, group);
}

inline int32_t BuildChannelDumpString(const HcommDfxChannelDumpData &dump,
                                      const std::vector<std::vector<HcommDfxQueueEntry>> &fullEntries,
                                      std::string &out)
{
    out.clear();
    if (dump.magic != HCOMM_DFX_CHANNEL_DUMP_MAGIC || dump.size < sizeof(HcommDfxChannelDumpData)) {
        DfxAppend(out, "[HcommDfxDump] invalid dump magic=0x%x size=%u\n", dump.magic, dump.size);
        return HCOMM_FAILED;
    }
    const uint32_t channelNum = std::min<uint32_t>(dump.channelNum, HCOMM_DFX_MAX_CHANNELS);
    int32_t curDev = -1;
    (void)aclrtGetDevice(&curDev);
    for (uint32_t channelIdx = 0; channelIdx < channelNum; ++channelIdx) {
        ChannelEntity entity;
        std::memset(&entity, 0, sizeof(entity));
        if (dump.channels[channelIdx].valid != 0 && dump.channels[channelIdx].channelEntitySize > 0U) {
            std::memcpy(&entity, dump.channels[channelIdx].channelEntity,
                std::min<uint32_t>(dump.channels[channelIdx].channelEntitySize, sizeof(entity)));
        }
        DfxAppend(out, "[HcommDfxDump] ==== channel dump: curDev=%d handle=0x%llx engine=%d protocol=%d ====\n",
            curDev, static_cast<unsigned long long>(dump.channels[channelIdx].handle),
            static_cast<int32_t>(entity.engine), static_cast<int32_t>(entity.protocol));
        static const std::vector<HcommDfxQueueEntry> emptyEntries;
        const std::vector<HcommDfxQueueEntry> &entries =
            channelIdx < fullEntries.size() ? fullEntries[channelIdx] : emptyEntries;
        AppendChannel(out, channelIdx, dump.channels[channelIdx], entries);
    }
    EnsureDfxLinePrefix(out);
    return HCOMM_SUCCESS;
}

class HcommDfxParser {
public:
    static int32_t Build(const HcommDfxChannelDumpData &dump,
                         const std::vector<std::vector<HcommDfxQueueEntry>> &entries, std::string &out)
    {
        return BuildChannelDumpString(dump, entries, out);
    }
};

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_PARSE_H
