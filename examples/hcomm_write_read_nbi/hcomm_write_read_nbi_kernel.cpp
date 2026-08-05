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
#include "hcomm_rw_def.h"

namespace HcommExample {

class KernelHcommWriteRead {
public:
    __aicore__ inline KernelHcommWriteRead() {}

    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe *pipe)
    {
        tpipe_ = pipe;
        context_ = reinterpret_cast<__gm__ CommContext*>(context);
        channel_ = context_->channelHandle;
        localBuf_ = reinterpret_cast<GM_ADDR>(context_->localBufferAddr);
        remoteBuf_ = reinterpret_cast<GM_ADDR>(context_->remoteBufferAddr);
        rankId_ = context_->rankId;
        worldSize_ = context_->worldSize;

        // Hcomm内部WQE/CQE队列需要一块UB空间存放，调用Init分配并初始化工作空间
        tpipe_->InitBuffer(hcommBuf_, HCOMM_WORKSPACE_SIZE);
        hcommTensor_ = hcommBuf_.Get<uint8_t>();
        if (hcomm_.Init(hcommTensor_, HCOMM_WORKSPACE_SIZE) != AscendC::HCOMM_SUCCESS) {
            initOk_ = false;
        }
    }

    __aicore__ inline void Process()
    {
        context_->testResult = 0;
        context_->mismatchIndex = 0;
        context_->actualValue = 0;
        context_->expectedValue = 0;
        // worldSize_是总卡数，必须>=2，不然无法构成通信的两个节点
        if (!initOk_ || worldSize_ < MIN_RANKS || rankId_ >= worldSize_) {
            Fail(1);
            return;
        }

        DoWriteAndRead();
    }

private:
    // 两卡对称执行WriteNbi+ReadNbi，通过偏移区分四段地址：
    //   seg0: [0, DATA_SIZE)             本卡pattern（Host预初始化，随机值）
    //   seg1: [DATA_SIZE, 2*DATA_SIZE)   接收对端WriteNbi写入的数据
    //   seg2: [2*DATA_SIZE, 3*DATA_SIZE) 接收本卡ReadNbi从对端seg0读回的数据
    __aicore__ inline void DoWriteAndRead()
    {
        // WriteNbi(channel, dst=远端seg1, src=本地seg0, len)：将本地pattern写入对端seg1
        GM_ADDR localSrc = reinterpret_cast<GM_ADDR>(localBuf_);
        GM_ADDR remoteDst = remoteBuf_ + DATA_SIZE;
        if (hcomm_.WriteNbi(channel_, remoteDst, localSrc, DATA_SIZE) != AscendC::HCOMM_SUCCESS) {
            Fail(2);
            return;
        }

        // ReadNbi(channel, dst=本地seg2, src=远端seg0, len)：从对端seg0读回pattern到本地seg2
        __gm__ uint8_t* readDst = localBuf_ + 2 * DATA_SIZE;
        GM_ADDR localDst = reinterpret_cast<GM_ADDR>(readDst);
        if (hcomm_.ReadNbi(channel_, localDst, remoteBuf_, DATA_SIZE) != AscendC::HCOMM_SUCCESS) {
            Fail(3);
            return;
        }

        // Drain轮询CQ等待通信任务完成，返回后数据可见性才有保证
        if (hcomm_.Drain(channel_) != AscendC::HCOMM_SUCCESS) {
            Fail(4);
            return;
        }
    }

    __aicore__ inline void Fail(uint32_t code) { context_->testResult = code; }

    AscendC::TPipe *tpipe_{nullptr};
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    AscendC::LocalTensor<uint8_t> hcommTensor_;
    // COMM_PROTOCOL_UBC_CTP（URMA协议）适用于Ascend 950系列；RoCE协议使用COMM_PROTOCOL_ROCE
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm_;
    __gm__ CommContext* context_{nullptr};
    AscendC::ChannelHandle channel_{0};
    GM_ADDR localBuf_{nullptr};
    GM_ADDR remoteBuf_{nullptr};
    uint32_t rankId_{0};
    uint32_t worldSize_{0};
    bool initOk_{false};
};

} // namespace HcommExample

// Kernel入口
extern "C" __global__ __aicore__ void kernel_hcomm_write_read_nbi(GM_ADDR context)
{
    AscendC::TPipe pipe;
    HcommExample::KernelHcommWriteRead op;
    op.Init(context, &pipe);
    op.Process();
}