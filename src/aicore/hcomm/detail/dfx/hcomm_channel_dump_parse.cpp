/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_dfx_channel_dump_impl.h"

#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "../../common/hcomm_inner_def.h"
#undef HCOMM_INCLUDE_INTERNAL_HEADERS

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace AscendC {
namespace HcommDfx {
namespace {
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
} // namespace

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

} // namespace HcommDfx
} // namespace AscendC
