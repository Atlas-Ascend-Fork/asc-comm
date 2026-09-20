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
#include "hcomm/hcomm.h"

#include "roce_batch_write_read_def.h"

namespace HcommRoceBatchWriteReadExample {

class KernelRoceBatchWriteRead {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_ROCE>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ KernelContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteBuffer);
        pipe->InitBuffer(batchBuffer_, kBatchBufferBytes);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;

        auto batchHandle = hcomm_.MakeBatchHandle(
            static_cast<AscendC::ChannelHandle>(context_->channelHandle), batchTensor_, kBatchBufferBytes,
            remoteBuffer_ + kSendOffset, localBuffer_ + kSendOffset);
        if (batchHandle.channelHandle == 0U) {
            context_->result = KERNEL_MAKE_HANDLE_FAILED;
            context_->hcommResult = AscendC::HCOMM_FAILED;
            return;
        }

        for (uint32_t index = 0U; index < kOperationCount; ++index) {
            const uint64_t offset = static_cast<uint64_t>(index) * kTransferBytes;
            context_->hcommResult = hcomm_.WriteNbi(
                batchHandle, remoteBuffer_ + kWriteResultOffset + offset, localBuffer_ + kSendOffset + offset,
                kTransferBytes);
            if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_WRITE_FAILED;
                return;
            }
        }

        for (uint32_t index = 0U; index < kOperationCount; ++index) {
            const uint64_t offset = static_cast<uint64_t>(index) * kTransferBytes;
            context_->hcommResult = hcomm_.ReadNbi(
                batchHandle, localBuffer_ + kReadResultOffset + offset, remoteBuffer_ + kSendOffset + offset,
                kTransferBytes);
            if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_READ_FAILED;
                return;
            }
        }

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

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
};

} // namespace HcommRoceBatchWriteReadExample

extern "C" __vector__ __global__ __aicore__ void hcomm_roce_batch_write_read_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommRoceBatchWriteReadExample::KernelRoceBatchWriteRead op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchHcommRoceBatchWriteRead(void* stream, uint64_t context)
{
    hcomm_roce_batch_write_read_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
