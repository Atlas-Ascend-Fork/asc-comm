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

#include "hcomm_batch_write_def.h"

namespace HcommBatchWriteExample {

class KernelHcommBatchWrite {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP>;

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
        auto multiBatchHandle = hcomm_.MakeBatchHandle(
            static_cast<AscendC::MultiChannelHandle>(context_->multiChannel), batchTensor_, BATCH_BUFFER_SIZE);

        for (uint32_t channelIndex = 0U; channelIndex < context_->channelNum; ++channelIndex) {
            GM_ADDR remoteAddr = reinterpret_cast<GM_ADDR>(context_->remoteBuffers[channelIndex]) + RECV_BASE_OFFSET;
            auto& peerBatchHandle = hcomm_.GetHandleRef(multiBatchHandle, channelIndex, remoteAddr);
            context_->hcommResult = hcomm_.WriteNbi(peerBatchHandle, remoteAddr, localBuffer_ + SEND_OFFSET, DATA_SIZE);
            if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_WRITE_FAILED;
                return;
            }
        }
        context_->hcommResult = hcomm_.BatchCommit(multiBatchHandle);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }
        context_->hcommResult = hcomm_.Drain(multiBatchHandle);
        if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ BatchContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
};

} // namespace HcommBatchWriteExample

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommBatchWriteExample::KernelHcommBatchWrite op;
    op.Init(context, &pipe);
    op.Process();
}
