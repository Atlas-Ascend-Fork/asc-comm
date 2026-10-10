/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file roce_multi_sge_kernel.cpp
 * \brief Device side of the Hcomm RoCE multi-SGE batch read/write example.
 */

#include "roce_multi_sge_def.h"

#include "hcomm/hcomm.h"

#include "kernel_operator.h"

namespace HcommRoceMultiSge {

class KernelHcommRoceMultiSge {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_ROCE>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ KernelContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(static_cast<uintptr_t>(context_->localBuffer));
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(static_cast<uintptr_t>(context_->remoteBuffer));
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_SIZE);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;

        auto batchHandle = hcomm_.MakeBatchHandle(
            static_cast<AscendC::ChannelHandle>(context_->channel), batchTensor_, BATCH_BUFFER_SIZE, remoteBuffer_,
            localBuffer_);
        if (batchHandle.channelHandle == 0U) {
            context_->result = KERNEL_MAKE_BATCH_HANDLE_FAILED;
            return;
        }

        if (context_->operation == KERNEL_WRITE) {
            RunWrite(batchHandle);
        } else {
            RunRead(batchHandle);
        }

        if (context_->result != KERNEL_SUCCESS) {
            return;
        }
        CommitAndDrain(batchHandle);
    }

private:
    __aicore__ inline void CommitAndDrain(AscendC::UbcBatchHandle& batchHandle)
    {
        context_->hcommResult = hcomm_.BatchCommit(batchHandle);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }
        context_->hcommResult = hcomm_.Drain(batchHandle);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

    __aicore__ inline void RunWrite(AscendC::UbcBatchHandle& batchHandle)
    {
        AscendC::BufDesc srcDescs[SGE_COUNT];
        for (uint32_t call = 0U; call < CALL_COUNT; ++call) {
            const uint64_t localBase = reinterpret_cast<uint64_t>(localBuffer_);
            const uint64_t remoteBase = reinterpret_cast<uint64_t>(remoteBuffer_);
            const uint64_t scatteredOffset = SOURCE_SCATTERED_OFFSET + call * SGE_COUNT * SEGMENT_STRIDE;
            GM_ADDR dst = reinterpret_cast<GM_ADDR>(remoteBase + WRITE_DST_OFFSET + call * MESSAGE_SIZE);
            for (uint32_t segment = 0U; segment < SGE_COUNT; ++segment) {
                srcDescs[segment].addr =
                    reinterpret_cast<GM_ADDR>(localBase + scatteredOffset + segment * SEGMENT_STRIDE);
                srcDescs[segment].len = SEGMENT_SIZE;
            }
            context_->hcommResult = hcomm_.WriteNbi(batchHandle, dst, srcDescs, SGE_COUNT);
            if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_WRITE_FAILED;
                return;
            }
        }
    }

    __aicore__ inline void RunRead(AscendC::UbcBatchHandle& batchHandle)
    {
        AscendC::BufDesc dstDescs[SGE_COUNT];
        for (uint32_t call = 0U; call < CALL_COUNT; ++call) {
            const uint64_t localBase = reinterpret_cast<uint64_t>(localBuffer_);
            const uint64_t remoteBase = reinterpret_cast<uint64_t>(remoteBuffer_);
            const uint64_t scatteredOffset = READ_DST_OFFSET + call * SGE_COUNT * SEGMENT_STRIDE;
            GM_ADDR src = reinterpret_cast<GM_ADDR>(remoteBase + SOURCE_CONTIGUOUS_OFFSET + call * MESSAGE_SIZE);
            for (uint32_t segment = 0U; segment < SGE_COUNT; ++segment) {
                dstDescs[segment].addr =
                    reinterpret_cast<GM_ADDR>(localBase + scatteredOffset + segment * SEGMENT_STRIDE);
                dstDescs[segment].len = SEGMENT_SIZE;
            }
            context_->hcommResult = hcomm_.ReadNbi(batchHandle, dstDescs, SGE_COUNT, src);
            if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_READ_FAILED;
                return;
            }
        }
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
};

} // namespace HcommRoceMultiSge

extern "C" __vector__ __global__ __aicore__ void hcomm_roce_multi_sge_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommRoceMultiSge::KernelHcommRoceMultiSge op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchHcommRoceMultiSge(void* stream, uint64_t context)
{
    hcomm_roce_multi_sge_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
