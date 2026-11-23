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
 * \brief Device side of the Hcomm RoCE multi-SGE read/write example.
 */

#include "roce_multi_sge_def.h"
#include "kernel_operator.h"
#include "asccomm/asccomm.h"

namespace HcommRoceMultiSge {

constexpr asc::wqe_entry ROCE_MULTI_SGE_NO_CQE_CFG = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

class KernelHcommRoceMultiSge {
    using AsccommType = asc::asccomm<asc::COMM_PROTOCOL_ROCE>;

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
        context_->hcommResult = asc::ASCCOMM_SUCCESS;

        context_->hcommResult =
            asccomm_.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(batchTensor_.GetPhyAddr()), BATCH_BUFFER_SIZE);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_INIT_FAILED;
            return;
        }

        if (context_->submissionMode == KERNEL_SUBMISSION_NORMAL) {
            RunNormal();
        } else {
            RunBatch();
        }
        if (context_->result != KERNEL_SUCCESS) {
            return;
        }
    }

private:
    __aicore__ inline void FillWriteDescs(uint32_t call, asc::buf_desc (&srcDescs)[SGE_COUNT])
    {
        const uint64_t localBase = reinterpret_cast<uint64_t>(localBuffer_);
        const uint64_t scatteredOffset = SOURCE_SCATTERED_OFFSET + call * SGE_COUNT * SEGMENT_STRIDE;
        for (uint32_t segment = 0U; segment < SGE_COUNT; ++segment) {
            srcDescs[segment].addr = reinterpret_cast<GM_ADDR>(localBase + scatteredOffset + segment * SEGMENT_STRIDE);
            srcDescs[segment].len = SEGMENT_SIZE;
        }
    }

    __aicore__ inline GM_ADDR WriteDestination(uint32_t call)
    {
        const uint64_t remoteBase = reinterpret_cast<uint64_t>(remoteBuffer_);
        return reinterpret_cast<GM_ADDR>(remoteBase + WRITE_DST_OFFSET + call * MESSAGE_SIZE);
    }

    __aicore__ inline void FillReadDescs(uint32_t call, asc::buf_desc (&dstDescs)[SGE_COUNT])
    {
        const uint64_t localBase = reinterpret_cast<uint64_t>(localBuffer_);
        const uint64_t scatteredOffset = READ_DST_OFFSET + call * SGE_COUNT * SEGMENT_STRIDE;
        for (uint32_t segment = 0U; segment < SGE_COUNT; ++segment) {
            dstDescs[segment].addr = reinterpret_cast<GM_ADDR>(localBase + scatteredOffset + segment * SEGMENT_STRIDE);
            dstDescs[segment].len = SEGMENT_SIZE;
        }
    }

    __aicore__ inline GM_ADDR ReadSource(uint32_t call)
    {
        const uint64_t remoteBase = reinterpret_cast<uint64_t>(remoteBuffer_);
        return reinterpret_cast<GM_ADDR>(remoteBase + SOURCE_CONTIGUOUS_OFFSET + call * MESSAGE_SIZE);
    }

    __aicore__ inline void RunBatch()
    {
        auto batchHandle = asccomm_.asccomm_make_batch_handle(
            static_cast<AscendC::ChannelHandle>(context_->channel),
            reinterpret_cast<__ubuf__ uint8_t*>(batchTensor_.GetPhyAddr()), BATCH_BUFFER_SIZE, remoteBuffer_,
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

        context_->hcommResult = asccomm_.asccomm_batch_commit(batchHandle);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }

        context_->hcommResult = asccomm_.asccomm_drain(batchHandle);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

    __aicore__ inline void RunNormal()
    {
        if (context_->operation == KERNEL_WRITE) {
            RunNormalWrite();
        } else {
            RunNormalRead();
        }
        if (context_->result != KERNEL_SUCCESS) {
            return;
        }

        context_->hcommResult = asccomm_.asccomm_commit(static_cast<AscendC::ChannelHandle>(context_->channel));
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_COMMIT_FAILED;
            return;
        }

        context_->hcommResult = asccomm_.asccomm_drain(static_cast<AscendC::ChannelHandle>(context_->channel));
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_DRAIN_FAILED;
        }
    }

    __aicore__ inline void RunNormalWrite()
    {
        asc::buf_desc srcDescs[SGE_COUNT];
        FillWriteDescs(NORMAL_CALL_INDEX, srcDescs);
        context_->hcommResult = asccomm_.asccomm_write_nbi<false>(
            static_cast<AscendC::ChannelHandle>(context_->channel), WriteDestination(NORMAL_CALL_INDEX), srcDescs,
            SGE_COUNT);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_WRITE_FAILED;
        }
    }

    __aicore__ inline void RunNormalRead()
    {
        asc::buf_desc dstDescs[SGE_COUNT];
        FillReadDescs(NORMAL_CALL_INDEX, dstDescs);
        context_->hcommResult = asccomm_.asccomm_read_nbi<false>(
            static_cast<AscendC::ChannelHandle>(context_->channel), dstDescs, SGE_COUNT, ReadSource(NORMAL_CALL_INDEX));
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_READ_FAILED;
        }
    }

    __aicore__ inline void RunWrite(asc::ubc_batch_handle& batchHandle)
    {
        asc::buf_desc srcDescs[SGE_COUNT];
        for (uint32_t call = 0U; call + 1U < CALL_COUNT; ++call) {
            FillWriteDescs(call, srcDescs);
            context_->hcommResult = asccomm_.asccomm_write_nbi<ROCE_MULTI_SGE_NO_CQE_CFG>(
                batchHandle, WriteDestination(call), srcDescs, SGE_COUNT);
            if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
                context_->result = KERNEL_WRITE_FAILED;
                return;
            }
        }

        const uint64_t lastCall = CALL_COUNT - 1U;
        FillWriteDescs(static_cast<uint32_t>(lastCall), srcDescs);
        GM_ADDR dst = WriteDestination(static_cast<uint32_t>(lastCall));
        context_->hcommResult = asccomm_.asccomm_write_nbi(batchHandle, dst, srcDescs, SGE_COUNT);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_WRITE_FAILED;
        }
    }

    __aicore__ inline void RunRead(asc::ubc_batch_handle& batchHandle)
    {
        asc::buf_desc dstDescs[SGE_COUNT];
        for (uint32_t call = 0U; call + 1U < CALL_COUNT; ++call) {
            FillReadDescs(call, dstDescs);
            context_->hcommResult = asccomm_.asccomm_read_nbi<ROCE_MULTI_SGE_NO_CQE_CFG>(
                batchHandle, dstDescs, SGE_COUNT, ReadSource(call));
            if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
                context_->result = KERNEL_READ_FAILED;
                return;
            }
        }

        const uint64_t lastCall = CALL_COUNT - 1U;
        FillReadDescs(static_cast<uint32_t>(lastCall), dstDescs);
        GM_ADDR src = ReadSource(static_cast<uint32_t>(lastCall));
        context_->hcommResult = asccomm_.asccomm_read_nbi(batchHandle, dstDescs, SGE_COUNT, src);
        if (context_->hcommResult != asc::ASCCOMM_SUCCESS) {
            context_->result = KERNEL_READ_FAILED;
        }
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    AsccommType asccomm_;
    __gm__ KernelContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
};

} // namespace HcommRoceMultiSge

extern "C" __vector__ __global__ __aicore__ void asccomm_roce_multi_sge_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommRoceMultiSge::KernelHcommRoceMultiSge op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchHcommRoceMultiSge(void* stream, uint64_t context)
{
    asccomm_roce_multi_sge_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
