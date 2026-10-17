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

#include "rdv_def.h"

namespace RdvHcommExample {

// Weakly ordered WQEs (odr=0, fence=0) let the hardware pipeline WQEs freely.
constexpr AscendC::WqeEntry WEAK_ORDER_CQE_CONFIG = {
    .odr = 0U,
    .fence = 0U,
    .se = 0U,
    .cqe = 1U,
    .inlineEn = 0U,
};

constexpr AscendC::WqeEntry WEAK_ORDER_NO_CQE_CONFIG = {
    .odr = 0U,
    .fence = 0U,
    .se = 0U,
    .cqe = 0U,
    .inlineEn = 0U,
};

class KernelRdv {
    using MultiBatchHandle = AscendC::BatchHandle<AscendC::MultiChannelHandle>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        coreIndex_ = static_cast<uint32_t>(AscendC::GetBlockIdx());
        context_ = reinterpret_cast<__gm__ BatchContext*>(context) + coreIndex_;
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        sendOffset_ = SEND_BASE_OFFSET + static_cast<uint64_t>(coreIndex_) * DATA_SIZE;
        remoteRecvOffset_ =
            RECV_BASE_OFFSET + (static_cast<uint64_t>(context_->myRank) * AIV_CORE_NUM + coreIndex_) * DATA_SIZE;
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_SIZE);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;
        auto multiBatch = hcomm_.MakeBatchHandle(
            static_cast<AscendC::MultiChannelHandle>(context_->multiChannel), batchTensor_, BATCH_BUFFER_SIZE);
        if (multiBatch.channelNum == 0U) {
            context_->result = KERNEL_MAKE_BATCH_HANDLE_FAILED;
            return;
        }
        // The SQ depth is decided by the Host channel resources; read it from the handle instead of assuming a value.
        const uint32_t sqDepth = multiBatch.handle.sqContext.contextInfo.ubJfs.sqDepth;
        context_->sqDepth = sqDepth;
        context_->cqDepth = multiBatch.handle.cqContext.contextInfo.ubJfc.cqDepth;
        if (sqDepth == 0U) {
            context_->result = KERNEL_MAKE_BATCH_HANDLE_FAILED;
            return;
        }

        for (uint32_t channelSlot = 0U; channelSlot < context_->channelNum; ++channelSlot) {
            remoteBuffers_[channelSlot] = reinterpret_cast<GM_ADDR>(context_->remoteBuffers[channelSlot]);
        }

        AscendC::SyncAll<true>();
        uint32_t wqesPerPeerPerCommit = context_->wqesPerPeerPerCommit;
        uint32_t committedBbSinceDrain = 0U;
        for (uint32_t iteration = 0U; iteration < PERF_ITERATION_COUNT; ++iteration) {
            uint32_t remainingWqes = WQES_PER_PEER_PER_ITERATION;
            while (remainingWqes > 0U) {
                uint32_t channelBb = context_->channelNum * WQE_BB_NUM;
                uint32_t availableWqes = (sqDepth - committedBbSinceDrain) / channelBb;
                if (availableWqes == 0U) {
                    if (!RunDrain(multiBatch)) {
                        return;
                    }
                    committedBbSinceDrain = 0U;
                    continue;
                }
                uint32_t wqesThisCommit = remainingWqes > wqesPerPeerPerCommit ? wqesPerPeerPerCommit : remainingWqes;
                wqesThisCommit = wqesThisCommit > availableWqes ? availableWqes : wqesThisCommit;
                uint32_t committedBbAfterCommit = committedBbSinceDrain + wqesThisCommit * channelBb;
                uint32_t remainingWqesAfterCommit = remainingWqes - wqesThisCommit;
                bool drainAfterCommit = remainingWqesAfterCommit == 0U;
                if (!drainAfterCommit) {
                    drainAfterCommit = (sqDepth - committedBbAfterCommit) / channelBb == 0U;
                }
                if (!RunCommit(multiBatch, wqesThisCommit, drainAfterCommit)) {
                    return;
                }
                committedBbSinceDrain = committedBbAfterCommit;
                remainingWqes = remainingWqesAfterCommit;
                if (drainAfterCommit) {
                    if (!RunDrain(multiBatch)) {
                        return;
                    }
                    committedBbSinceDrain = 0U;
                }
            }
        }
        if (committedBbSinceDrain != 0U) {
            if (!RunDrain(multiBatch)) {
                return;
            }
        }
    }

private:
    __aicore__ inline bool RunCommit(MultiBatchHandle& multiBatch, uint32_t writesPerChannel, bool requestCompletion)
    {
        for (uint32_t writeIndex = 0U; writeIndex < writesPerChannel; ++writeIndex) {
            for (uint32_t channelSlot = 0U; channelSlot < context_->channelNum; ++channelSlot) {
                bool requestCqe = requestCompletion && writeIndex + 1U == writesPerChannel;
                GM_ADDR remoteDst = remoteBuffers_[channelSlot] + remoteRecvOffset_;
                GM_ADDR localSrc = localBuffer_ + sendOffset_;
                auto& peerBatch = hcomm_.GetHandleRef(multiBatch, channelSlot, remoteDst);
                if (peerBatch.channelHandle == 0U) {
                    context_->result = KERNEL_GET_HANDLE_REF_FAILED;
                    return false;
                }
                if (requestCqe) {
                    context_->hcommResult =
                        hcomm_.WriteNbi<WEAK_ORDER_CQE_CONFIG>(peerBatch, remoteDst, localSrc, DATA_SIZE);
                } else {
                    context_->hcommResult =
                        hcomm_.WriteNbi<WEAK_ORDER_NO_CQE_CONFIG>(peerBatch, remoteDst, localSrc, DATA_SIZE);
                }
                if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                    context_->result = KERNEL_WRITE_FAILED;
                    return false;
                }
            }
        }
        AscendC::SyncAll<true>();
        context_->hcommResult = hcomm_.BatchCommit(multiBatch);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return false;
        }
        return true;
    }

    __aicore__ inline bool RunDrain(MultiBatchHandle& multiBatch)
    {
        context_->hcommResult = hcomm_.Drain(multiBatch);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
            return false;
        }
        return true;
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm_;
    GM_ADDR remoteBuffers_[MAX_CHANNEL_NUM];
    __gm__ BatchContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    uint32_t coreIndex_{0U};
    uint64_t sendOffset_{0U};
    uint64_t remoteRecvOffset_{0U};
};

} // namespace RdvHcommExample

extern "C" __vector__ __global__ __aicore__ void rdv_hcomm_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    RdvHcommExample::KernelRdv op;
    op.Init(context, &pipe);
    op.Process();
}
