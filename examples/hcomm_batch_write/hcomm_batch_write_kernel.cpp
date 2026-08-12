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

class KernelHcommBatchWrite {
public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ BatchContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteBuffer);
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_SIZE);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        AscendC::BatchHandle batch = hcomm_.MakeBatchHandle(context_->multiChannel, batchTensor_, BATCH_BUFFER_SIZE);
        if (batch.stateAddr == 0U) {
            context_->result = KERNEL_MAKE_BATCH_HANDLE_FAILED;
            return;
        }

        AscendC::BatchHandleRef channel0 = hcomm_.GetHandleRef(batch, 0U, remoteBuffer_);
        AscendC::BatchHandleRef channel1 = hcomm_.GetHandleRef(batch, 1U, remoteBuffer_);
        if (channel0.stateAddr == 0U || channel1.stateAddr == 0U) {
            context_->result = KERNEL_GET_HANDLE_REF_FAILED;
            return;
        }

        if (hcomm_.WriteNbi(channel0, remoteBuffer_ + RECV_OFFSET_0, localBuffer_ + SEND_OFFSET_0, DATA_SIZE) !=
            AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WRITE_FAILED;
            return;
        }
        if (hcomm_.WriteNbi(channel1, remoteBuffer_ + RECV_OFFSET_1, localBuffer_ + SEND_OFFSET_1, DATA_SIZE) !=
            AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WRITE_FAILED;
            return;
        }
        if (hcomm_.BatchCommit(batch) != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }
        if (hcomm_.Drain(batch) != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm_;
    __gm__ BatchContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
};

} // namespace HcommBatchWriteExample

extern "C" __vector__ __global__ __aicore__ void hcomm_batch_write_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommBatchWriteExample::KernelHcommBatchWrite op;
    op.Init(context, &pipe);
    op.Process();
}
