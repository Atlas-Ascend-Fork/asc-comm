/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adv_api/hcomm/hcomm.h"
#include "kernel_operator.h"

#include "hcomm_batch_write_def.h"

namespace HcommBatchWriteExample {

constexpr AscendC::UrmaWqeEntry NO_CQE_CONFIG = {
    .odr = 5U,
    .fence = 1U,
    .se = 0U,
    .cqe = 0U,
    .inlineEn = 0U,
};

class KernelHcommBatchWrite {
public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        uint32_t coreIndex = static_cast<uint32_t>(AscendC::GetBlockIdx());
        context_ = reinterpret_cast<__gm__ BatchContext*>(context) + coreIndex;
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        sendOffset_ = SEND_BASE_OFFSET + static_cast<uint64_t>(coreIndex) * DATA_SIZE;
        receiveOffset_ = RECV_BASE_OFFSET + static_cast<uint64_t>(coreIndex) * DATA_SIZE;
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_SIZE);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process(bool performance)
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;
        context_->startCycles = 0U;
        context_->endCycles = 0U;
        AscendC::BatchHandle batch = hcomm_.MakeBatchHandle(context_->multiChannel, batchTensor_, BATCH_BUFFER_SIZE);
        if (batch.stateAddr == 0U) {
            context_->result = KERNEL_MAKE_BATCH_HANDLE_FAILED;
            return;
        }

        for (uint32_t channelIndex = 0U; channelIndex < context_->channelNum; ++channelIndex) {
            channels_[channelIndex] = hcomm_.GetHandleRef(batch, channelIndex);
            if (channels_[channelIndex].stateAddr == 0U) {
                context_->result = KERNEL_GET_HANDLE_REF_FAILED;
                return;
            }
        }

        uint32_t loopCount = performance ? PERF_LOOP_COUNT : WARMUP_LOOP_COUNT;
        if (performance) {
            AscendC::PipeBarrier<PIPE_ALL>();
            context_->startCycles = AscendC::GetSystemCycle();
        }
        for (uint32_t loop = 0U; loop < loopCount; ++loop) {
            if (!RunBatch(batch)) {
                return;
            }
        }
        if (performance) {
            AscendC::PipeBarrier<PIPE_ALL>();
            context_->endCycles = AscendC::GetSystemCycle();
        }
    }

private:
    __aicore__ inline bool RunBatch(AscendC::BatchHandle batch)
    {
        for (uint32_t writeIndex = 0U; writeIndex < WRITES_PER_CHANNEL_PER_BATCH; ++writeIndex) {
            bool requestCompletion = writeIndex + 1U == WRITES_PER_CHANNEL_PER_BATCH;
            for (uint32_t channelIndex = 0U; channelIndex < context_->channelNum; ++channelIndex) {
                GM_ADDR remoteBuffer = reinterpret_cast<GM_ADDR>(context_->remoteBuffers[channelIndex]);
                if (requestCompletion) {
                    context_->hcommResult = hcomm_.WriteNbi(
                        channels_[channelIndex], remoteBuffer + receiveOffset_, localBuffer_ + sendOffset_, DATA_SIZE);
                } else {
                    context_->hcommResult = hcomm_.WriteNbi<NO_CQE_CONFIG>(
                        channels_[channelIndex], remoteBuffer + receiveOffset_, localBuffer_ + sendOffset_, DATA_SIZE);
                }
                if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                    context_->result = KERNEL_WRITE_FAILED;
                    return false;
                }
            }
        }
        context_->hcommResult = hcomm_.BatchCommit(batch);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return false;
        }
        context_->hcommResult = hcomm_.Drain(batch);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
            return false;
        }
        return true;
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm_;
    AscendC::BatchHandleRef channels_[MAX_CHANNEL_NUM];
    __gm__ BatchContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    uint64_t sendOffset_{0U};
    uint64_t receiveOffset_{0U};
};

} // namespace HcommBatchWriteExample

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_kernel(GM_ADDR context, bool performance)
{
    AscendC::TPipe pipe;
    HcommBatchWriteExample::KernelHcommBatchWrite op;
    op.Init(context, &pipe);
    op.Process(performance);
}
