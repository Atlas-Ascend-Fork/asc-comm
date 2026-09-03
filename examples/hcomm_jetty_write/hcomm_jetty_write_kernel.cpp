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
#include "hcomm_jetty_write_def.h"

namespace HcommExample {

class KernelHcommJettyWrite {
public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ CommContext*>(context);
        localBuffer_ = reinterpret_cast<GM_ADDR>(context_->localBufferAddr);
        jettyTable_ = reinterpret_cast<__gm__ void*>(context_->jettyTable);
        pipe->InitBuffer(peerBuffer_, sizeof(AscendC::HcommJettyPeerInfo));
        pipe->InitBuffer(jettyInfoBuffer_, sizeof(AscendC::HcommJettyInfo));
        pipe->InitBuffer(sqeBuffer_, AscendC::HcommJetty::kNumMaxSqeBytes);
        peerTensor_ = peerBuffer_.Get<uint8_t>();
        jettyInfoTensor_ = jettyInfoBuffer_.Get<uint8_t>();
        sqeTensor_ = sqeBuffer_.Get<uint8_t>();
    }

    __aicore__ inline void Process()
    {
        context_->testResult = TEST_SUCCESS;
        auto peerInfo = reinterpret_cast<__ubuf__ AscendC::HcommJettyPeerInfo*>(peerTensor_.GetPhyAddr());
        auto jettyInfo = reinterpret_cast<__ubuf__ AscendC::HcommJettyInfo*>(jettyInfoTensor_.GetPhyAddr());
        auto sqe = reinterpret_cast<__ubuf__ uint8_t*>(sqeTensor_.GetPhyAddr());
        const AscendC::HcommPeer peer(peerInfo, jettyTable_, 0U);
        AscendC::HcommJetty jetty(jettyInfo, sqe, jettyTable_, 0U);
        if (jetty.template Put<1000000>(peer, localBuffer_, localBuffer_ + WRITE_RESULT_OFFSET,
            localBuffer_ + SEND_DATA_OFFSET, DATA_SIZE) != AscendC::HCOMM_SUCCESS ||
            jetty.template Drain<1000000>() != AscendC::HCOMM_SUCCESS) {
            context_->testResult = TEST_DRAIN_FAILED;
        }
    }

private:
    AscendC::TBuf<AscendC::TPosition::VECOUT> peerBuffer_;
    AscendC::TBuf<AscendC::TPosition::VECOUT> jettyInfoBuffer_;
    AscendC::TBuf<AscendC::TPosition::VECOUT> sqeBuffer_;
    AscendC::LocalTensor<uint8_t> peerTensor_;
    AscendC::LocalTensor<uint8_t> jettyInfoTensor_;
    AscendC::LocalTensor<uint8_t> sqeTensor_;
    __gm__ CommContext* context_{nullptr};
    GM_ADDR localBuffer_{nullptr};
    __gm__ void* jettyTable_{nullptr};
};

__aicore__ inline void RunHcommKernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    KernelHcommJettyWrite op;
    op.Init(context, &pipe);
    op.Process();
}

} // namespace HcommExample

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_jetty_write(GM_ADDR context)
{
    HcommExample::RunHcommKernel(context);
}
