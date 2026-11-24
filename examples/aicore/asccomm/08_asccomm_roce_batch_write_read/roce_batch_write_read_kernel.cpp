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
#include "asccomm/asccomm.h"

#include "roce_batch_write_read_def.h"

namespace HcommRoceBatchWriteReadExample {

constexpr asc::wqe_entry ROCE_BATCH_NO_CQE_CFG = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

class KernelRoceBatchWriteRead {
    using AsccommType = asc::asccomm<asc::COMM_PROTOCOL_ROCE>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ KernelContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBuffer);
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteBuffer);
        pipe->InitBuffer(batchBuffer_, BATCH_BUFFER_BYTES);
        batchTensor_ = batchBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        context_->hcommResult = asc::ASCCOMM_SUCCESS;

        auto batchHandle = asccomm_.make_batch_handle(
            static_cast<AscendC::ChannelHandle>(context_->channelHandle),
            reinterpret_cast<__ubuf__ uint8_t*>(batchTensor_.GetPhyAddr()), BATCH_BUFFER_BYTES,
            remoteBuffer_ + SEND_DATA_OFFSET, localBuffer_ + SEND_DATA_OFFSET);
        if (batchHandle.channelHandle == 0U) {
            context_->result = KERNEL_MAKE_HANDLE_FAILED;
            context_->hcommResult = asc::ASCCOMM_FAILED;
            return;
        }

        for (uint32_t index = 0U; index + 1U < OPERATION_COUNT; ++index) {
            const uint64_t offset = static_cast<uint64_t>(index) * TRANSFER_BYTES;
            context_->hcommResult = asccomm_.write_nbi<ROCE_BATCH_NO_CQE_CFG>(
                batchHandle, remoteBuffer_ + WRITE_RESULT_OFFSET + offset, localBuffer_ + SEND_DATA_OFFSET + offset,
                TRANSFER_BYTES);
            if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
                context_->result = KERNEL_WRITE_FAILED;
                return;
            }
        }

        const uint64_t writeLastOffset = static_cast<uint64_t>(OPERATION_COUNT - 1U) * TRANSFER_BYTES;
        context_->hcommResult = asccomm_.write_nbi(
            batchHandle, remoteBuffer_ + WRITE_RESULT_OFFSET + writeLastOffset,
            localBuffer_ + SEND_DATA_OFFSET + writeLastOffset, TRANSFER_BYTES);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_WRITE_FAILED;
            return;
        }

        for (uint32_t index = 0U; index + 1U < OPERATION_COUNT; ++index) {
            const uint64_t offset = static_cast<uint64_t>(index) * TRANSFER_BYTES;
            context_->hcommResult = asccomm_.read_nbi<ROCE_BATCH_NO_CQE_CFG>(
                batchHandle, localBuffer_ + READ_RESULT_OFFSET + offset, remoteBuffer_ + SEND_DATA_OFFSET + offset,
                TRANSFER_BYTES);
            if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
                context_->result = KERNEL_READ_FAILED;
                return;
            }
        }

        const uint64_t readLastOffset = static_cast<uint64_t>(OPERATION_COUNT - 1U) * TRANSFER_BYTES;
        context_->hcommResult = asccomm_.read_nbi(
            batchHandle, localBuffer_ + READ_RESULT_OFFSET + readLastOffset,
            remoteBuffer_ + SEND_DATA_OFFSET + readLastOffset, TRANSFER_BYTES);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_READ_FAILED;
            return;
        }

        context_->hcommResult = asccomm_.batch_commit(batchHandle);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }

        context_->hcommResult = asccomm_.drain(batchHandle);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    AsccommType asccomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
};

} // namespace HcommRoceBatchWriteReadExample

extern "C" __vector__ __global__ __aicore__ void asccomm_roce_batch_write_read_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommRoceBatchWriteReadExample::KernelRoceBatchWriteRead op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchHcommRoceBatchWriteRead(void* stream, uint64_t context)
{
    asccomm_roce_batch_write_read_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
