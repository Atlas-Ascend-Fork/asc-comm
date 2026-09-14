/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
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

#include "../../common/hcomm_inner_def.h"
#include "../common/hcomm_dfx_impl.h"

namespace AscendC {
namespace HcommDfx {

// One dump is assembled into a single string and emitted with one write, so concurrent dumps from multiple ranks
// never interleave line-by-line (item: "一次dump打印合成一条").
inline void DfxAppend(std::string& out, const char* fmt, ...)
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

inline void AppendBuffer(std::string& out, const char* tag, uint32_t idx, const HcommDfxBufferDump& dump)
{
    if (dump.rawSize < sizeof(RegedBufferEntity)) {
        DfxAppend(out, "[HcommDfxDump] %s[%u] rawSize=%u (truncated)\n", tag, idx, dump.rawSize);
        return;
    }
    RegedBufferEntity buffer;
    std::memcpy(&buffer, dump.raw, std::min<uint32_t>(dump.rawSize, static_cast<uint32_t>(sizeof(buffer))));
    const uint64_t bufAddr = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.addr : buffer.bufferInfo.rma.addr;
    const uint64_t bufSize = buffer.type == REGED_BUFFER_IPC ? buffer.bufferInfo.ipc.size : buffer.bufferInfo.rma.size;
    DfxAppend(
        out, "[HcommDfxDump] %s[%u] type=%d addr=0x%llx size=%llu\n", tag, idx, static_cast<int32_t>(buffer.type),
        static_cast<unsigned long long>(bufAddr), static_cast<unsigned long long>(bufSize));
}

// Emits the head/tail values without a validity suffix; a read failure becomes an explicit ERROR line instead
// (item: "解析失败就报错，去掉valid=1").
inline void AppendQueueCursorErrors(std::string& out, const char* kind, uint32_t idx, const HcommDfxQueueCtxDump& dump)
{
    if (dump.headValid == 0U) {
        DfxAppend(out, "[HcommDfxDump] %s[%u] ERROR: head read failed (device pointer unreadable)\n", kind, idx);
    }
    if (dump.tailValid == 0U) {
        DfxAppend(out, "[HcommDfxDump] %s[%u] ERROR: tail read failed (device pointer unreadable)\n", kind, idx);
    }
}

inline void AppendSqContext(std::string& out, uint32_t idx, const HcommDfxQueueCtxDump& dump)
{
    SqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == SQ_CONTEXT_TYPE_UB_JFS) {
        const auto& jfs = ctx.contextInfo.ubJfs;
        DfxAppend(
            out,
            "[HcommDfxDump] SqContext[%u] type=UB_JFS jfsID=%u sqVa=0x%llx depth=%u wqeSize=%u dbVa=0x%llx "
            "head=%u tail=%u\n",
            idx, jfs.jfsID, static_cast<unsigned long long>(jfs.sqVa), jfs.sqDepth, jfs.wqeSize,
            static_cast<unsigned long long>(jfs.dbVa), dump.head, dump.tail);
    } else {
        DfxAppend(
            out, "[HcommDfxDump] SqContext[%u] type=%d (non-URMA, raw only) head=%u tail=%u\n", idx,
            static_cast<int32_t>(ctx.type), dump.head, dump.tail);
    }
    AppendQueueCursorErrors(out, "SqContext", idx, dump);
}

inline void AppendCqContext(std::string& out, uint32_t idx, const HcommDfxQueueCtxDump& dump)
{
    CqContext ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    std::memcpy(&ctx, dump.raw, std::min<uint32_t>(dump.rawSize, sizeof(ctx)));
    if (ctx.type == CQ_CONTEXT_TYPE_UB_JFC) {
        const auto& jfc = ctx.contextInfo.ubJfc;
        DfxAppend(
            out,
            "[HcommDfxDump] CqContext[%u] type=UB_JFC jfcID=%u cqVa=0x%llx depth=%u cqeSize=%u dbVa=0x%llx "
            "head=%u tail=%u\n",
            idx, jfc.jfcID, static_cast<unsigned long long>(jfc.scqVa), jfc.cqDepth, jfc.cqeSize,
            static_cast<unsigned long long>(jfc.dbVa), dump.head, dump.tail);
    } else {
        DfxAppend(
            out, "[HcommDfxDump] CqContext[%u] type=%d (non-URMA, raw only) head=%u tail=%u\n", idx,
            static_cast<int32_t>(ctx.type), dump.head, dump.tail);
    }
    AppendQueueCursorErrors(out, "CqContext", idx, dump);
}

// Prints the full SQE context (all HcommUrmaSqeCtx fields), then the trailing SGE, and the notify context for
// WRITE_WITH_NOTIFY — matching what the kernel-side HcommUrmaDumpWqeCtx emits (item: WQE 字段至少对齐 dump).
inline void AppendWqe(std::string& out, const HcommDfxQueueEntry& entry)
{
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        DfxAppend(
            out, "[HcommDfxDump] WQE[q=%u slot=%u cur=%u addr=0x%llx readStatus=%u (no payload)]\n", entry.queueIndex,
            entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr), entry.readStatus);
        return;
    }

    HcommUrmaSqeCtx wqe;
    std::memset(&wqe, 0, sizeof(wqe));
    std::memcpy(&wqe, entry.data, std::min<uint32_t>(entry.size, sizeof(wqe)));

    DfxAppend(
        out, "[HcommDfxDump] WQE[q=%u slot=%u cur=%u addr=0x%llx size=%u]\n", entry.queueIndex, entry.entryIndex,
        entry.cursor, static_cast<unsigned long long>(entry.addr), entry.size);
    DfxAppend(
        out,
        "  sqeBbIdx=%u flag=0x%x rsv0=%u nf=%u tokenEn=%u rmtJettyType=%u owner=%u targetHint=0x%x "
        "opcode=0x%x rsv1=%u inlineMsgLen=%u tpId=0x%x\n",
        wqe.sqeBbIdx, wqe.flag, wqe.rsv0, wqe.nf, wqe.tokenEn, wqe.rmtJettyType, wqe.owner, wqe.targetHint, wqe.opcode,
        wqe.rsv1, wqe.inlineMsgLen, wqe.tpId);
    DfxAppend(
        out,
        "  sgeNum=%u rmtJettyOrSegId=0x%x rsv2=%u rmtEid=0x%llx%016llx rmtTokenValue=0x%x udfType=%u "
        "reduceDataType=%u reduceOpcode=%u rmtAddrLOrTokenId=0x%x rmtAddrHOrTokenValue=0x%x\n",
        wqe.sgeNum, wqe.rmtJettyOrSegId, wqe.rsv2, static_cast<unsigned long long>(wqe.rmtEidH),
        static_cast<unsigned long long>(wqe.rmtEidL), wqe.rmtTokenValue, wqe.udfType, wqe.reduceDataType,
        wqe.reduceOpcode, wqe.rmtAddrLOrTokenId, wqe.rmtAddrHOrTokenValue);

    uint32_t offset = static_cast<uint32_t>(sizeof(HcommUrmaSqeCtx));
    if (wqe.opcode == static_cast<uint32_t>(HcommUrmaOpCode::WRITE_WITH_NOTIFY)) {
        if (entry.size >= offset + sizeof(HcommUrmaNotifyCtx)) {
            HcommUrmaNotifyCtx notify;
            std::memcpy(&notify, entry.data + offset, sizeof(notify));
            DfxAppend(
                out, "  notify: tokenId=0x%x tokenValue=0x%x addrL=0x%x addrH=0x%x dataL=0x%x dataH=0x%x\n",
                notify.notifyTokenId, notify.notifyTokenValue, notify.notifyAddrL, notify.notifyAddrH,
                notify.notifyDataL, notify.notifyDataH);
            offset += static_cast<uint32_t>(sizeof(HcommUrmaNotifyCtx));
        } else {
            DfxAppend(out, "  notify: (truncated, size=%u)\n", entry.size);
        }
    }

    if (entry.size >= offset + sizeof(HcommUrmaSgeCtx)) {
        HcommUrmaSgeCtx sge;
        std::memcpy(&sge, entry.data + offset, sizeof(sge));
        DfxAppend(
            out, "  sge: len=%u tokenId=0x%x va=0x%llx\n", sge.len, sge.tokenId,
            static_cast<unsigned long long>(sge.va));
    } else {
        DfxAppend(out, "  sge: (truncated, size=%u)\n", entry.size);
    }
}

inline void AppendCqe(std::string& out, const HcommDfxQueueEntry& entry)
{
    if (entry.readStatus != HCOMM_DFX_READ_OK) {
        DfxAppend(
            out, "[HcommDfxDump] CQE[q=%u slot=%u cur=%u addr=0x%llx readStatus=%u (no payload)]\n", entry.queueIndex,
            entry.entryIndex, entry.cursor, static_cast<unsigned long long>(entry.addr), entry.readStatus);
        return;
    }
    HcommUrmaJfcCqeCtx cqe;
    std::memset(&cqe, 0, sizeof(cqe));
    std::memcpy(&cqe, entry.data, std::min<uint32_t>(entry.size, sizeof(cqe)));

    DfxAppend(
        out, "[HcommDfxDump] CQE[q=%u slot=%u cur=%u addr=0x%llx size=%u]\n", entry.queueIndex, entry.entryIndex,
        entry.cursor, static_cast<unsigned long long>(entry.addr), entry.size);
    DfxAppend(
        out,
        "  sR=%u isJetty=%u owner=%u inlineEn=%u opcode=%u fd=%u substatus=%u status=%u entryIdx=%u "
        "localNumL=%u localNumH=%u rmtIdx=0x%x tpn=0x%x byteCnt=%u\n",
        cqe.sR, cqe.isJetty, cqe.owner, cqe.inlineEn, cqe.opcode, cqe.fd, cqe.substatus, cqe.status, cqe.entryIdx,
        cqe.localNumL, cqe.localNumH, cqe.rmtIdx, cqe.tpn, cqe.byteCnt);
}

inline void AppendChannel(std::string& out, uint32_t channelIdx, const HcommDfxChannelInfo& channel)
{
    ChannelEntity entity;
    std::memset(&entity, 0, sizeof(entity));
    if (channel.valid != 0 && channel.channelEntitySize > 0U) {
        std::memcpy(&entity, channel.channelEntity, std::min<uint32_t>(channel.channelEntitySize, sizeof(entity)));
    }
    DfxAppend(
        out,
        "[HcommDfxDump] Channel[%u] handle=0x%llx valid=%u engine=%d protocol=%d localBufNum=%u "
        "remoteBufNum=%u sqNum=%u cqNum=%u sqHead=%u sqTail=%u cqHead=%u cqTail=%u\n",
        channelIdx, static_cast<unsigned long long>(channel.handle), channel.valid, static_cast<int32_t>(entity.engine),
        static_cast<int32_t>(entity.protocol), entity.localBufferNum, entity.remoteBufferNum, entity.sqNum,
        entity.cqNum, entity.sqHead, entity.sqTail, entity.cqHead, entity.cqTail);

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
    const uint32_t entryNum = std::min<uint32_t>(channel.queueEntryDumpNum, HCOMM_DFX_MAX_QUEUE_ENTRIES);
    for (uint32_t idx = 0; idx < entryNum; ++idx) {
        const HcommDfxQueueEntry& entry = channel.queueEntries[idx];
        if (entry.entryType == HCOMM_DFX_ENTRY_URMA_WQE) {
            AppendWqe(out, entry);
        } else {
            AppendCqe(out, entry);
        }
    }
}

inline int32_t ParseAndPrintChannelDump(const HcommDfxChannelDumpData& dump)
{
    std::string out;
    if (dump.magic != HCOMM_DFX_CHANNEL_DUMP_MAGIC || dump.size < sizeof(HcommDfxChannelDumpData)) {
        DfxAppend(out, "[HcommDfxDump] invalid dump magic=0x%x size=%u\n", dump.magic, dump.size);
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
        return HCOMM_FAILED;
    }
    if (dump.version != HCOMM_DFX_CHANNEL_DUMP_VERSION) {
        DfxAppend(
            out, "[HcommDfxDump] version mismatch dump=%u expect=%u, continuing best-effort\n", dump.version,
            HCOMM_DFX_CHANNEL_DUMP_VERSION);
    }
    const uint32_t channelNum = std::min<uint32_t>(dump.channelNum, HCOMM_DFX_MAX_CHANNELS);
    DfxAppend(out, "[HcommDfxDump] ==== channel dump: channelNum=%u ====\n", channelNum);
    for (uint32_t channelIdx = 0; channelIdx < channelNum; ++channelIdx) {
        AppendChannel(out, channelIdx, dump.channels[channelIdx]);
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
    return HCOMM_SUCCESS;
}

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_PARSE_H
