/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/hcomm.h"
#include "kernel_operator.h"

#include "hcomm_batch_write_performance_def.h"

namespace HcommBatchWritePerformanceExample {

class KernelHcommBatchWritePerformance {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP>;
    using PeerBatchHandleType = AscendC::BatchHandle<AscendC::ChannelHandle>;
    using MultiBatchHandleType = AscendC::BatchHandle<AscendC::MultiChannelHandle>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ BatchContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_SIZE);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;
        context_->elapsedCycles = 0U;
        context_->completedOperations = 0U;
        channelNum_ = context_->channelNum;
        operationsPerBatch_ = context_->operationsPerBatch;
        uint32_t warmupBatches = context_->warmupBatches;
        uint32_t measureBatches = context_->measureBatches;
        if (channelNum_ == 0U || operationsPerBatch_ == 0U || operationsPerBatch_ > MAX_OPERATIONS_PER_BATCH ||
            measureBatches == 0U) {
            context_->result = KERNEL_INVALID_ARGUMENT;
            return;
        }
        for (uint32_t channelIndex = 0U; channelIndex < channelNum_; ++channelIndex) {
            remoteBuffers_[channelIndex] = context_->remoteBuffers[channelIndex];
        }

        auto multiBatchHandle = hcomm_.MakeBatchHandle(
            static_cast<AscendC::MultiChannelHandle>(context_->multiChannel), batchTensor_, BATCH_BUFFER_SIZE);
        (void)RunBenchmark(multiBatchHandle, warmupBatches, measureBatches);
    }

private:
    __aicore__ inline bool RunBenchmark(
        MultiBatchHandleType& multiBatchHandle, uint32_t warmupBatches, uint32_t measureBatches)
    {
        for (uint32_t batchIndex = 0U; batchIndex < warmupBatches; ++batchIndex) {
            if (!PostAndCommitBatch(multiBatchHandle) || !DrainBatch(multiBatchHandle)) {
                return false;
            }
        }

        uint64_t elapsedCycles = 0U;
        for (uint32_t batchIndex = 0U; batchIndex < measureBatches; ++batchIndex) {
            for (uint32_t channelIndex = 0U; channelIndex < channelNum_; ++channelIndex) {
                GM_ADDR remoteBase = reinterpret_cast<GM_ADDR>(remoteBuffers_[channelIndex]);
                GM_ADDR remoteAddr = remoteBase + RECV_BASE_OFFSET;
                auto& peerBatchHandle = hcomm_.GetHandleRef(multiBatchHandle, channelIndex, remoteAddr);

                AscendC::PipeBarrier<PIPE_ALL>();
                uint64_t beginCycle = static_cast<uint64_t>(AscendC::GetSystemCycle());
                if (!PostChannelNotifies(peerBatchHandle, channelIndex, remoteBase)) {
                    return false;
                }
                AscendC::PipeBarrier<PIPE_ALL>();
                uint64_t endCycle = static_cast<uint64_t>(AscendC::GetSystemCycle());
                elapsedCycles += endCycle - beginCycle;
            }

            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t beginCycle = static_cast<uint64_t>(AscendC::GetSystemCycle());
            if (!CommitBatch(multiBatchHandle)) {
                return false;
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t endCycle = static_cast<uint64_t>(AscendC::GetSystemCycle());
            elapsedCycles += endCycle - beginCycle;
            if (!DrainBatch(multiBatchHandle)) {
                return false;
            }
        }
        context_->elapsedCycles = elapsedCycles;
        context_->completedOperations = static_cast<uint64_t>(operationsPerBatch_) * measureBatches;
        return true;
    }

    __aicore__ inline bool PostAndCommitBatch(MultiBatchHandleType& multiBatchHandle)
    {
        for (uint32_t channelIndex = 0U; channelIndex < channelNum_; ++channelIndex) {
            GM_ADDR remoteBase = reinterpret_cast<GM_ADDR>(remoteBuffers_[channelIndex]);
            GM_ADDR remoteAddr = remoteBase + RECV_BASE_OFFSET;
            auto& peerBatchHandle = hcomm_.GetHandleRef(multiBatchHandle, channelIndex, remoteAddr);
            if (!PostChannelNotifies(peerBatchHandle, channelIndex, remoteBase)) {
                return false;
            }
        }
        return CommitBatch(multiBatchHandle);
    }

    __aicore__ inline bool PostChannelNotifies(
        PeerBatchHandleType& peerBatchHandle, uint32_t channelIndex, GM_ADDR remoteBase)
    {
        GM_ADDR remoteAddr = remoteBase + RECV_BASE_OFFSET;
        GM_ADDR notifyAddr = remoteBase + NOTIFY_OFFSET;
        for (uint32_t operationIndex = channelIndex; operationIndex < operationsPerBatch_;
             operationIndex += channelNum_) {
            int32_t ret = hcomm_.WriteWithNotifyNbi(
                peerBatchHandle, remoteAddr, localBuffer_ + SEND_OFFSET, DATA_SIZE, notifyAddr, NOTIFY_VALUE);
            if (ret != AscendC::HCOMM_SUCCESS) {
                context_->hcommResult = ret;
                context_->result = KERNEL_NOTIFY_FAILED;
                return false;
            }
        }
        return true;
    }

    __aicore__ inline bool CommitBatch(MultiBatchHandleType& multiBatchHandle)
    {
        int32_t ret = hcomm_.BatchCommit(multiBatchHandle);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->hcommResult = ret;
            context_->result = KERNEL_COMMIT_FAILED;
            return false;
        }
        return true;
    }

    __aicore__ inline bool DrainBatch(MultiBatchHandleType& multiBatchHandle)
    {
        int32_t ret = hcomm_.Drain(multiBatchHandle);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->hcommResult = ret;
            context_->result = KERNEL_DRAIN_FAILED;
            return false;
        }
        return true;
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ BatchContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    uint64_t remoteBuffers_[MAX_CHANNEL_NUM]{};
    uint32_t channelNum_{0U};
    uint32_t operationsPerBatch_{0U};
};

} // namespace HcommBatchWritePerformanceExample

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_performance_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommBatchWritePerformanceExample::KernelHcommBatchWritePerformance op;
    op.Init(context, &pipe);
    op.Process();
}
