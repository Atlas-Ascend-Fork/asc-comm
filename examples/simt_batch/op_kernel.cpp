/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "hcomm/hcomm_simt.h"
#include "simt_batch_common.h"

namespace {
using namespace simt_batch;

constexpr uint32_t kBatchBufferBytes = AscendC::simt::HcommSimtBatchBufferBytes(1U);

__simt_vf__ __launch_bounds__(32) inline void SimtBatchVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* batchBuffer,
    __gm__ int32_t* status)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::Hcomm<> hcomm;
    int32_t ret = hcomm.Init(nullptr, 0U);
    if (ret != 0) {
        status[0] = ret;
        return;
    }

    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 1U);
    if (batch.context == nullptr) {
        status[0] = -1;
        return;
    }

    ret = hcomm.WriteNbi(batch, remote, local, kPayloadBytes);
    if (ret != 0) {
        status[0] = ret;
        return;
    }
    ret = hcomm.BatchCommit(batch);
    if (ret != 0) {
        status[0] = ret;
        return;
    }
    status[0] = hcomm.Drain(batch);
}

} // namespace

extern "C" __global__ __vector__ void SimtBatch(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    alignas(128) __ubuf__ uint64_t batchBuffer[kBatchBufferBytes / sizeof(uint64_t)];
    asc_vf_call<SimtBatchVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), status);
}

void LaunchSimtBatch(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status)
{
    SimtBatch<<<1, nullptr, stream>>>(
        channel, sendBufIdx, recvBufIdx, reinterpret_cast<int32_t*>(status));
}
