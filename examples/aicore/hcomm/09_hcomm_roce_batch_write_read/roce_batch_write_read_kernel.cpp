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
 * \file roce_batch_write_read_kernel.cpp
 * \brief Device side of the Hcomm RoCE batch write/read example.
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
        for (uint32_t index = 0U; index < MEMORY_SLICE_COUNT; ++index) {
            localBuffers_[index] = reinterpret_cast<GM_ADDR>(context_->localBuffers[index]);
            remoteBuffers_[index] = reinterpret_cast<GM_ADDR>(context_->remoteBuffers[index]);
        }
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_BYTES);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = AscendC::HCOMM_SUCCESS;

        const AscendC::ChannelHandle channel = static_cast<AscendC::ChannelHandle>(context_->channelHandle);
        auto* channelEntity = reinterpret_cast<__gm__ AscendC::ChannelEntity*>(channel);
        PrintChannelEntity(channelEntity);

        for (uint32_t slice = 0U; slice < MEMORY_SLICE_COUNT; ++slice) {
            auto batchHandle = hcomm_.MakeBatchHandle(
                channel, batchTensor_, BATCH_BUFFER_BYTES, remoteBuffers_[slice] + SEND_DATA_OFFSET,
                localBuffers_[slice] + SEND_DATA_OFFSET);
            if (batchHandle.channelHandle == 0U) {
                context_->result = KERNEL_MAKE_HANDLE_FAILED;
                context_->hcommResult = AscendC::HCOMM_FAILED;
                return;
            }

            const uint32_t begin = slice * OPERATION_PER_MEMORY;
            const uint32_t end = begin + OPERATION_PER_MEMORY;
            for (uint32_t index = begin; index < end; ++index) {
                const uint64_t offset = static_cast<uint64_t>(index % OPERATION_PER_MEMORY) * TRANSFER_BYTES;
                context_->hcommResult = hcomm_.WriteNbi(
                    batchHandle, remoteBuffers_[slice] + WRITE_RESULT_OFFSET + offset,
                    localBuffers_[slice] + SEND_DATA_OFFSET + offset, TRANSFER_BYTES);
                if (context_->hcommResult != AscendC::HCOMM_SUCCESS) {
                    context_->result = KERNEL_WRITE_FAILED;
                    return;
                }
            }

            for (uint32_t index = begin; index < end; ++index) {
                const uint64_t offset = static_cast<uint64_t>(index % OPERATION_PER_MEMORY) * TRANSFER_BYTES;
                context_->hcommResult = hcomm_.ReadNbi(
                    batchHandle, localBuffers_[slice] + READ_RESULT_OFFSET + offset,
                    remoteBuffers_[slice] + SEND_DATA_OFFSET + offset, TRANSFER_BYTES);
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
                return;
            }
        }
    }

private:
    __aicore__ inline void PrintChannelEntity(__gm__ AscendC::ChannelEntity* channelEntity)
    {
        AscendC::PRINTF(
            "ChannelEntity localBufferNum=%u remoteBufferNum=%u\n", channelEntity->localBufferNum,
            channelEntity->remoteBufferNum);
        for (uint32_t index = 0U; index < channelEntity->localBufferNum; ++index) {
            const auto& buffer = channelEntity->localBufferAddr[index];
            AscendC::PRINTF(
                "localBuffer[%u] addr=0x%llx size=%llu lkey=0x%x rkey=0x%x\n", index, buffer.bufferInfo.rma.addr,
                buffer.bufferInfo.rma.size, buffer.bufferInfo.rma.protectionInfo.memInfo.roce.lkey,
                buffer.bufferInfo.rma.protectionInfo.memInfo.roce.rkey);
        }
        for (uint32_t index = 0U; index < channelEntity->remoteBufferNum; ++index) {
            const auto& buffer = channelEntity->remoteBufferAddr[index];
            AscendC::PRINTF(
                "remoteBuffer[%u] addr=0x%llx size=%llu lkey=0x%x rkey=0x%x\n", index, buffer.bufferInfo.rma.addr,
                buffer.bufferInfo.rma.size, buffer.bufferInfo.rma.protectionInfo.memInfo.roce.lkey,
                buffer.bufferInfo.rma.protectionInfo.memInfo.roce.rkey);
        }
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localBuffers_[MEMORY_SLICE_COUNT]{};
    GM_ADDR remoteBuffers_[MEMORY_SLICE_COUNT]{};
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
