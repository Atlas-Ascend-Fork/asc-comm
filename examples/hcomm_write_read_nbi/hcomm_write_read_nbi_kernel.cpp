/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>

#include "kernel_operator.h"
#include "hcomm_rw_def.h"

namespace HcommExample {

class KernelHcommWriteRead {
public:
    __aicore__ inline void init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ CommContext*>(context);
        channel_ = context_->channel_handle;
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->local_buffer_addr);
        remoteBuffer_ = reinterpret_cast<GM_ADDR>(context_->remote_buffer_addr);

        pipe->InitBuffer(hcommBuf_, HCOMM_WORKSPACE_SIZE);
        hcommTensor_ = hcommBuf_.Get<uint8_t>();
        if (hcomm_.init(hcommTensor_, HCOMM_WORKSPACE_SIZE) == AscendC::asccomm_success) {
            initOk_ = true;
        }
    }

    template <uint32_t operationType>
    __aicore__ inline void Process()
    {
        static_assert(
            operationType == HCOMM_READ_ONLY || operationType == HCOMM_WRITE_ONLY || operationType == HCOMM_READ_WRITE,
            "Unsupported asccomm operation type");
        context_->test_result = TEST_SUCCESS;
        if (!initOk_) {
            context_->test_result = TEST_HCOMM_INIT_FAILED;
            return;
        }

        if constexpr ((operationType & HCOMM_WRITE_ONLY) != 0U) {
            DoWrite();
            if (context_->test_result != TEST_SUCCESS) {
                return;
            }
        }

        if constexpr ((operationType & HCOMM_READ_ONLY) != 0U) {
            DoRead();
            if (context_->test_result != TEST_SUCCESS) {
                return;
            }
        }

        if (hcomm_.drain(channel_) != AscendC::asccomm_success) {
            context_->test_result = TEST_DRAIN_FAILED;
        }
    }

private:
    __aicore__ inline void DoWrite()
    {
        GM_ADDR writeSrc = localBuffer_ + SEND_DATA_OFFSET;
        GM_ADDR writeDst = remoteBuffer_ + WRITE_RESULT_OFFSET;
        if (hcomm_.write_nbi(channel_, writeDst, writeSrc, DATA_SIZE) != AscendC::asccomm_success) {
            context_->test_result = TEST_WRITE_FAILED;
        }
    }

    __aicore__ inline void DoRead()
    {
        GM_ADDR readDst = localBuffer_ + READ_RESULT_OFFSET;
        GM_ADDR readSrc = remoteBuffer_ + SEND_DATA_OFFSET;
        if (hcomm_.read_nbi(channel_, readDst, readSrc, DATA_SIZE) != AscendC::asccomm_success) {
            context_->test_result = TEST_READ_FAILED;
        }
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    AscendC::LocalTensor<uint8_t> hcommTensor_;
    AscendC::asccomm<KERNEL_COMM_PROTOCOL> hcomm_;
    __gm__ CommContext* context_{nullptr};
    AscendC::channel_handle channel_{0};
    GM_ADDR localBuffer_{nullptr};
    GM_ADDR remoteBuffer_{nullptr};
    bool initOk_{false};
};

template <uint32_t operationType>
__aicore__ inline void RunHcommKernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    KernelHcommWriteRead op;
    op.init(context, &pipe);
    op.Process<operationType>();
}

} // namespace HcommExample

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_read_nbi(GM_ADDR context)
{
    HcommExample::RunHcommKernel<HCOMM_READ_ONLY>(context);
}

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_write_nbi(GM_ADDR context)
{
    HcommExample::RunHcommKernel<HCOMM_WRITE_ONLY>(context);
}

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_write_read_nbi(GM_ADDR context)
{
    HcommExample::RunHcommKernel<HCOMM_READ_WRITE>(context);
}
