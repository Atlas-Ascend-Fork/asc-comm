/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Reused from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7
// Original source: custom_op_ccu/op_kernel/all_to_all_v_write_custom_v3.cpp

#include "kernel_operator.h"
#include "kernel_operator_intf.h"
#include "all_to_all_v_write_bench_tiling.h"

using namespace AscendC;

namespace {
constexpr uint64_t WAIT_REPORT_MAGIC = 0xA5A55A5AA5A55A5AULL;
constexpr uint64_t NS_PER_CYCLE = 1ULL;
} // namespace

__aicore__ inline void CopyFromGmToGmScalar(GM_ADDR dst, GM_ADDR src, uint32_t bytes)
{
    uint32_t copied = 0;
    if (((reinterpret_cast<uint64_t>(src) | reinterpret_cast<uint64_t>(dst)) & 7U) == 0) {
        // Preserve the legacy wide copy for aligned communication payloads.
        GlobalTensor<uint64_t> gmSrc, gmDst;
        gmSrc.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(src), bytes / 8U);
        gmDst.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(dst), bytes / 8U);
        for (uint32_t i = 0; i < bytes / 8U; ++i) {
            gmDst.SetValue(i, gmSrc.GetValue(i));
        }
        copied = bytes / 8U * 8U;
    }
    if (copied < bytes) {
        // INT8/FP16 peers can end on any byte, including unaligned tiny cases.
        GlobalTensor<uint8_t> tailSrc, tailDst;
        tailSrc.SetGlobalBuffer(reinterpret_cast<__gm__ uint8_t*>(src), bytes);
        tailDst.SetGlobalBuffer(reinterpret_cast<__gm__ uint8_t*>(dst), bytes);
        for (uint32_t i = copied; i < bytes; ++i) {
            tailDst.SetValue(i, tailSrc.GetValue(i));
        }
    }
}

__aicore__ inline void FlushDataCacheGM(GM_ADDR gmAddr)
{
    GlobalTensor<uint64_t> globalTensorCache;
    globalTensorCache.SetGlobalBuffer((__gm__ uint64_t*)gmAddr);
    AscendC::DataCacheCleanAndInvalid<uint64_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(
        globalTensorCache);
}

extern "C" __global__ __aicore__ void all_to_all_v_write_bench(
    GM_ADDR input, GM_ADDR input_splits, GM_ADDR output_splits, GM_ADDR output, GM_ADDR workspace, GM_ADDR tiling)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIC_ONLY);
    if (GetBlockIdx() != 0 || input == nullptr || output == nullptr || workspace == nullptr || tiling == nullptr ||
        GetUserWorkspace(workspace) == nullptr) {
        return;
    }

    REGISTER_TILING_DEFAULT(AllToAllVWriteBenchTilingData);
    GET_TILING_DATA_WITH_STRUCT(AllToAllVWriteBenchTilingData, tiling_data, tiling);

    constexpr uint32_t MAX_RANK_DIM = 384;
    const uint64_t K = tiling_data.param.K;
    const uint64_t rowBytes = K * tiling_data.param.dataTypeSize;

    static constexpr HcclServerConfig HCCL_CFG = {CoreType::ON_AIC, 0};
    Hccl<HcclServerType::HCCL_SERVER_TYPE_CCU, HCCL_CFG> hccl;
    // Do not overwrite input payload before communication has consumed it.
    const auto communication_context = GetHcclContext<HCCL_GROUP_ID_0>();
    if (communication_context == nullptr)
        return;
    hccl.InitV2(communication_context, &tiling_data);
    if (hccl.SetCcTilingV2(offsetof(AllToAllVWriteBenchTilingData, mc2CcTiling)) != 0)
        return;

    const uint32_t rank_dim = hccl.GetRankDim();
    if (rank_dim == 0 || rank_dim > MAX_RANK_DIM || rank_dim != tiling_data.param.rankDim ||
        (tiling_data.param.dataTypeSize != 1 && tiling_data.param.dataTypeSize != 2 &&
         tiling_data.param.dataTypeSize != 4 && tiling_data.param.dataTypeSize != 8) ||
        K == 0 || tiling_data.param.receiveWindow == 0) {
        return;
    }
    const uint32_t rank_id = hccl.GetRankId();
    if (rank_id >= rank_dim)
        return;

    // The old receive-copy path uses a public SDK address instead of parsing private context layouts.
    GM_ADDR cclBufAddr = reinterpret_cast<GM_ADDR>(tiling_data.param.receiveWindow);

    __gm__ uint64_t* sendSizes = reinterpret_cast<__gm__ uint64_t*>(workspace);
    __gm__ uint64_t* sendOffsets = sendSizes + rank_dim * 2U;
    const uint64_t sendBytes = rowBytes; // 每个 rank 一个目的 rank 收到的字节数
    const uint64_t sizeA = sendBytes / 2U;
    for (uint32_t d = 0; d < rank_dim; ++d) {
        sendSizes[d] = sizeA;
        sendSizes[d + rank_dim] = sendBytes - sizeA;
        sendOffsets[d] = static_cast<uint64_t>(d) * sendBytes;
        sendOffsets[d + rank_dim] = static_cast<uint64_t>(d) * sendBytes + sizeA;
    }
    const uint64_t splitBytes = rank_dim * 2U * sizeof(uint64_t) * 2U;
    for (uint64_t off = 0; off < splitBytes; off += 64U) {
        FlushDataCacheGM((GM_ADDR)((uint64_t)sendSizes + off));
    }

    const uint64_t slotStride = (sendBytes + 511U) / 512U * 512U;
    const uint64_t remoteWinOffset = static_cast<uint64_t>(rank_id) * slotStride;
    const uint64_t localDataSize = sendBytes; // 发给自己的片（A+B 总量）

    const auto handle =
        hccl.AlltoAllvWrite<true>(input, (GM_ADDR)sendOffsets, (GM_ADDR)sendSizes, remoteWinOffset, localDataSize);
    if (handle < 0)
        return;
    const int64_t waitStartCycle = GetSystemCycle();
    const auto wait_status = hccl.Wait(handle);
    const uint64_t waitNs = static_cast<uint64_t>(GetSystemCycle() - waitStartCycle) * NS_PER_CYCLE;
    if (wait_status != 0)
        return;
    hccl.Finalize();

    {
        uint64_t dstElem = 0;
        const uint64_t elemsPerSrc = K;
        for (uint32_t s = 0; s < rank_dim; ++s) {
            const uint64_t srcBase = static_cast<uint64_t>(s) * slotStride;
            for (uint64_t offset = 0; offset < elemsPerSrc; offset += 4U) {
                const uint64_t copyElems = (elemsPerSrc - offset) > 4U ? 4U : (elemsPerSrc - offset);
                GM_ADDR dstAddr = (GM_ADDR)((uint64_t)output + (dstElem + offset) * tiling_data.param.dataTypeSize);
                GM_ADDR srcAddr = (GM_ADDR)((uint64_t)cclBufAddr + srcBase + offset * tiling_data.param.dataTypeSize);
                CopyFromGmToGmScalar(
                    dstAddr, srcAddr, static_cast<uint32_t>(copyElems) * tiling_data.param.dataTypeSize);
            }
            dstElem += elemsPerSrc;
        }
    }

    GlobalTensor<uint64_t> waitReport;
    waitReport.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(input));
    waitReport.SetValue(0, WAIT_REPORT_MAGIC);
    waitReport.SetValue(1, waitNs);
    DataCacheCleanAndInvalid<uint64_t, CacheLine::SINGLE_CACHE_LINE>(waitReport);
    (void)input_splits;
    (void)output_splits;
}
