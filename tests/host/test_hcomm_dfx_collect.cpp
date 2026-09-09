/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cassert>
#include <memory>
#include <vector>
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "../../src/aicore/hcomm/dfx/common/hcomm_dfx_impl.h"
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_IMPL_HCOMM_DFX_QUEUE_READER_KERNEL_H
namespace AscendC::HcommDfx {
inline void LaunchQueueRead(HcommDfxQueueReadArgs args, aclrtStream)
{
    auto* result = reinterpret_cast<HcommDfxQueueReadResult*>(args.resultAddr);
    result->ret = 0;
    result->size = args.size;
    std::memcpy(result->data, reinterpret_cast<void*>(args.srcAddr), args.size);
}
} // namespace AscendC::HcommDfx
#include "../../src/aicore/hcomm/dfx/host/hcomm_dfx_collect.h"

int main()
{
    using namespace AscendC;
    using namespace AscendC::HcommDfx;
    unsetenv("HCOMM_DFX_CURSOR_SOURCE");
    auto info = std::make_unique<HcommDfxChannelInfo>();
    RegedBufferEntity buffers[2]{};
    buffers[1].bufferInfo.rma.addr = 42;
    failedAddresses.insert(reinterpret_cast<uint64_t>(&buffers[0]));
    assert(!CollectBuffers(reinterpret_cast<uint64_t>(buffers), 2, info->localBuffers, info->localBufferDumpNum));
    assert(info->localBufferDumpNum == 2 && info->localBuffers[0].rawSize == 0);
    assert(info->localBuffers[1].rawSize == sizeof(RegedBufferEntity));
    std::string printed;
    AppendBuffer(printed, "localBuffer", 0, info->localBuffers[0]);
    assert(printed.find("ERROR") != std::string::npos);
    failedAddresses.clear();

    SqContext sq{};
    CqContext cq{};
    sq.type = SQ_CONTEXT_TYPE_UB_JFS;
    cq.type = CQ_CONTEXT_TYPE_UB_JFC;
    std::vector<uint8_t> queue(8 * 64);
    sq.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(queue.data());
    sq.contextInfo.ubJfs.sqDepth = 8;
    sq.contextInfo.ubJfs.wqeSize = 64;
    cq.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(queue.data());
    cq.contextInfo.ubJfc.cqDepth = 8;
    cq.contextInfo.ubJfc.cqeSize = 64;
    ChannelEntity entity{};
    entity.sqContextAddr = &sq;
    entity.cqContextAddr = &cq;
    entity.sqNum = entity.cqNum = 1;
    entity.sqHead = 2;
    entity.sqTail = 1;
    entity.cqHead = entity.cqTail = 2;
    void* reader = CreateQueueReader();
    assert(CollectChannel(reinterpret_cast<uint64_t>(&entity), *info, reader));
    assert(info->queueEntryDumpNum == 0); // Completed demo must not sample slot 1.
    assert(info->sqContexts[0].head == 2 && info->sqContexts[0].tail == 1);
    failedAddresses.insert(reinterpret_cast<uint64_t>(&sq));
    assert(!CollectChannel(reinterpret_cast<uint64_t>(&entity), *info, reader));
    assert(info->sqContexts[0].rawSize == 0 && info->cqContexts[0].rawSize != 0);
    failedAddresses.clear();
    entity.sqHead = 1;
    entity.sqTail = entity.cqHead = entity.cqTail = 0;
    syncError = 9;
    assert(!CollectChannel(reinterpret_cast<uint64_t>(&entity), *info, reader));
    assert(info->queueEntries[0].readStatus != HCOMM_DFX_READ_OK);
    assert(info->cqContexts[0].rawSize != 0); // WQE failure must not suppress CQ metadata.
    syncError = 0;
    setenv("HCOMM_DFX_CURSOR_SOURCE", "simt", 1);
    assert(!CollectChannel(reinterpret_cast<uint64_t>(&entity), *info, reader));
    assert(!info->sqContexts[0].headValid && !info->sqContexts[0].tailValid);
    uint64_t packedHead = 0;
    uint32_t tail = 0;
    sq.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&packedHead);
    sq.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&tail);
    cq.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&tail);
    assert(CollectChannel(reinterpret_cast<uint64_t>(&entity), *info, reader));
    assert(info->sqContexts[0].headValid && info->sqContexts[0].head == 0);
    // Two-BB WQE wraps at the end of the SQ. Its continuation is not a separate WQE.
    *info = {};
    HcommUrmaSqeCtx header{};
    header.opcode = static_cast<uint32_t>(HcommUrmaOpCode::WRITE_WITH_NOTIFY);
    std::memcpy(queue.data() + 7 * 64, &header, sizeof(header));
    assert(SampleSqEntries(0, reinterpret_cast<uint64_t>(queue.data()), 8, 64, 9, 7, *info, reader));
    assert(info->queueEntryDumpNum == 1 && info->queueEntries[0].size == 128);
    DestroyQueueReader(reader);
}
