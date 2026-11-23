/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file roce_atomic_inline_kernel.cpp
 * \brief Device side of the Hcomm RoCE atomic/inline example.
 */

#include <cstdint>

#include "roce_atomic_inline_def.h"
#include "kernel_operator.h"
#include "asccomm/asccomm.h"

namespace HcommRoceAtomicExample {

class KernelHcommRoceAtomicInline {
public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ CommContext*>(context);
        channel_ = context_->channelHandle;
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBufferAddr);
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(context_->remoteBufferAddr);

        pipe->InitBuffer(hcommBuf_, HCOMM_WORKSPACE_SIZE);
        hcommTensor_ = hcommBuf_.Get<uint8_t>();
        if (asccomm_.asccomm_init(
                reinterpret_cast<__ubuf__ uint8_t*>(hcommTensor_.GetPhyAddr()), HCOMM_WORKSPACE_SIZE) ==
            asc::ASCCOMM_SUCCESS) {
            initOk_ = true;
        }
    }

    __aicore__ inline void Process()
    {
        context_->testResult = TEST_SUCCESS;
        if (!initOk_) {
            context_->testResult = TEST_HCOMM_INIT_FAILED;
            return;
        }

        GM_ADDR remoteFaaValue = remoteBuffer_ + FAA_VALUE_OFFSET;
        GM_ADDR localFaaFetch = localBuffer_ + FAA_FETCH_OFFSET;
        if (asccomm_.asccomm_atomic_faa_nbi<uint64_t, false>(channel_, remoteFaaValue, localFaaFetch, FAA_ADD_VALUE) !=
            asc::ASCCOMM_SUCCESS) {
            context_->testResult = TEST_HCOMM_ATOMIC_FAA_FAILED;
            return;
        }

        GM_ADDR remoteCasValue = remoteBuffer_ + CAS_VALUE_OFFSET;
        GM_ADDR localCasFetch = localBuffer_ + CAS_FETCH_OFFSET;
        if (asccomm_.asccomm_atomic_cas_nbi<uint64_t, false>(
                channel_, remoteCasValue, localCasFetch, CAS_COMPARE_VALUE, CAS_SWAP_VALUE) != asc::ASCCOMM_SUCCESS) {
            context_->testResult = TEST_HCOMM_ATOMIC_CAS_FAILED;
            return;
        }

        GM_ADDR remoteInlineValue = remoteBuffer_ + INLINE_VALUE_OFFSET;
        if (asccomm_.asccomm_write_value_nbi<uint64_t, false>(channel_, remoteInlineValue, INLINE_WRITE_VALUE) !=
            asc::ASCCOMM_SUCCESS) {
            context_->testResult = TEST_HCOMM_WRITE_VALUE_FAILED;
            return;
        }

        if (asccomm_.asccomm_commit<PIPE_S>(channel_) != asc::ASCCOMM_SUCCESS) {
            context_->testResult = TEST_HCOMM_COMMIT_FAILED;
            return;
        }

        if (asccomm_.asccomm_drain<PIPE_MTE3>(channel_) != asc::ASCCOMM_SUCCESS) {
            context_->testResult = TEST_HCOMM_DRAIN_FAILED;
        }
    }

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    AscendC::LocalTensor<uint8_t> hcommTensor_;
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm_;
    __gm__ CommContext* context_{nullptr};
    AscendC::ChannelHandle channel_{0};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
    bool initOk_{false};
};

__aicore__ inline void RunHcommRoceAtomicInlineKernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    KernelHcommRoceAtomicInline op;
    op.Init(context, &pipe);
    op.Process();
}

} // namespace HcommRoceAtomicExample

extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_roce_atomic_inline(GM_ADDR context)
{
    HcommRoceAtomicExample::RunHcommRoceAtomicInlineKernel(context);
}

extern "C" void LaunchHcommRoceAtomicInline(
    uint64_t channel, uint64_t localBuffer, uint64_t remoteBuffer, __gm__ uint8_t* context, void* stream)
{
    (void)channel;
    (void)localBuffer;
    (void)remoteBuffer;
    kernel_asccomm_roce_atomic_inline<<<1, 0, stream>>>(context);
}
