/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file roce_batch_api_perf_kernel.cpp
 * \brief Device side of the Hcomm RoCE batch API performance example.
 */

#include "kernel_operator.h"
#include "hcomm/hcomm.h"

#include "roce_batch_api_perf_def.h"

namespace HcommRoceBatchApiPerfExample {

constexpr AscendC::UrmaWqeEntry ROCE_BATCH_NO_CQE_CFG = {
    .odr = 0,
    .fence = 0,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};

class KernelRoceBatchApiPerf {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_ROCE>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ KernelContext*>(context);
        localSendBuffer_ = reinterpret_cast<GM_ADDR>(context_->localSendBuffer);
        localRecvBuffer_ = reinterpret_cast<GM_ADDR>(context_->localRecvBuffer);
        remoteSendBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteSendBuffer);
        remoteRecvBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteRecvBuffer);
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_BYTES);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;

        AscendC::PipeBarrier<PIPE_ALL>();
        const uint64_t totalStart = AscendC::GetSystemCycle();
        if (!IsAligned64(localSendBuffer_) || !IsAligned64(localRecvBuffer_) || !IsAligned64(remoteSendBuffer_) ||
            !IsAligned64(remoteRecvBuffer_)) {
            context_->result = KERNEL_ADDRESS_UNALIGNED;
            context_->totalCycles = AscendC::GetSystemCycle() - totalStart;
            return;
        }
        auto batchHandle = hcomm_.MakeBatchHandle(
            static_cast<AscendC::ChannelHandle>(context_->channelHandle), batchTensor_, BATCH_BUFFER_BYTES,
            context_->api == PERF_API_WRITE ? remoteRecvBuffer_ : remoteSendBuffer_, localSendBuffer_);
        if (batchHandle.channelHandle == 0U) {
            context_->result = KERNEL_MAKE_HANDLE_FAILED;
            context_->totalCycles = AscendC::GetSystemCycle() - totalStart;
            return;
        }

        for (uint32_t run = 0U; run < TOTAL_RUN_COUNT; ++run) {
            const bool measure = run >= WARMUP_RUN_COUNT;
            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t runStart = AscendC::GetSystemCycle();
            if (context_->api == PERF_API_WRITE) {
                (void)SubmitBatch<PERF_API_WRITE>(batchHandle);
            } else {
                (void)SubmitBatch<PERF_API_READ>(batchHandle);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t issueElapsed = AscendC::GetSystemCycle() - runStart;

            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t commitStart = AscendC::GetSystemCycle();
            (void)hcomm_.BatchCommit(batchHandle);
            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t commitElapsed = AscendC::GetSystemCycle() - commitStart;

            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t drainStart = AscendC::GetSystemCycle();
            (void)hcomm_.Drain(batchHandle);
            AscendC::PipeBarrier<PIPE_ALL>();
            const uint64_t drainElapsed = AscendC::GetSystemCycle() - drainStart;

            if (measure) {
                AscendC::PipeBarrier<PIPE_ALL>();
                context_->totalCycles += AscendC::GetSystemCycle() - runStart;
                context_->issueCycles += issueElapsed;
                context_->commitCycles += commitElapsed;
                context_->drainCycles += drainElapsed;
            }
        }
    }

private:
    __aicore__ inline bool IsAligned64(GM_ADDR address) const
    {
        return (reinterpret_cast<uint64_t>(address) & (ROCE_ADDRESS_ALIGNMENT_BYTES - 1U)) == 0U;
    }

    template <uint32_t api>
    __aicore__ inline int32_t SubmitBatch(AscendC::UbcBatchHandle& batchHandle)
    {
        const uint64_t slotStride = GET_ROCE_SLOT_STRIDE(context_->dataSize);
        for (uint32_t i = 0U; i + 1U < context_->batchSize; ++i) {
            const uint64_t slotOffset = static_cast<uint64_t>(i) * slotStride;
            if constexpr (api == PERF_API_WRITE) {
                (void)hcomm_.WriteNbi<ROCE_BATCH_NO_CQE_CFG>(
                    batchHandle, remoteRecvBuffer_ + slotOffset, localSendBuffer_ + slotOffset, context_->dataSize);
            } else {
                (void)hcomm_.ReadNbi<ROCE_BATCH_NO_CQE_CFG>(
                    batchHandle, localRecvBuffer_ + slotOffset, remoteSendBuffer_ + slotOffset, context_->dataSize);
            }
        }

        // The final request in the batch generates the only CQE, so Drain can wait for all requests.
        const uint64_t lastSlotOffset = static_cast<uint64_t>(context_->batchSize - 1U) * slotStride;
        if constexpr (api == PERF_API_WRITE) {
            (void)hcomm_.WriteNbi(
                batchHandle, remoteRecvBuffer_ + lastSlotOffset, localSendBuffer_ + lastSlotOffset, context_->dataSize);
        } else {
            (void)hcomm_.ReadNbi(
                batchHandle, localRecvBuffer_ + lastSlotOffset, remoteSendBuffer_ + lastSlotOffset, context_->dataSize);
        }
        return AscendC::HCOMM_SUCCESS;
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localSendBuffer_{nullptr};
    GM_ADDR localRecvBuffer_{nullptr};
    GM_ADDR remoteSendBuffer_{nullptr};
    GM_ADDR remoteRecvBuffer_{nullptr};
};

} // namespace HcommRoceBatchApiPerfExample

extern "C" __vector__ __global__ __aicore__ void hcomm_roce_batch_api_perf_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommRoceBatchApiPerfExample::KernelRoceBatchApiPerf op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchHcommRoceBatchApiPerf(void* stream, uint64_t context)
{
    hcomm_roce_batch_api_perf_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
