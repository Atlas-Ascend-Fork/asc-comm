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
 * \file urma_normal_api_perf_kernel.cpp
 * \brief Device side of the URMA normal API performance example.
 */

#include "kernel_operator.h"
#include "hcomm/hcomm.h"

#include "urma_normal_api_perf_def.h"

namespace UrmaNormalApiPerfExample {

// commit=false 路径的 WQE 配置：同默认配置，仅 cqe=0（不依赖完成通知）
constexpr AscendC::UrmaWqeEntry WQE_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};

// MODE_* 同时是 timing 第一维槽位（与 def.h 的 API_*_IDX 同值），
// static_assert 编译期钉死这个 ABI。
enum TestApi : int32_t {
    MODE_WRITE = 0, // WriteNbi
    MODE_READ = 1,  // ReadNbi
    MODE_NOTIFY = 2 // WriteWithNotifyNbi
};

static_assert(
    MODE_WRITE == static_cast<int32_t>(API_WRITE_IDX) && MODE_READ == static_cast<int32_t>(API_READ_IDX) &&
        MODE_NOTIFY == static_cast<int32_t>(API_NOTIFY_IDX),
    "MODE_* values must match API_*_IDX timing slots");

class KernelUrmaNormalApiPerf {
    using HcommType = AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP>;

public:
    __aicore__ inline void Init(GM_ADDR context, AscendC::TPipe* pipe)
    {
        context_ = reinterpret_cast<__gm__ KernelContext*>(context);
        channel_ = context_->channelHandle;
        recvChannel_ = context_->recvChannelHandle;
        localBase_ = reinterpret_cast<GM_ADDR>(context_->localBufferAddr);
        remoteBase_ = reinterpret_cast<GM_ADDR>(context_->remoteBufferAddr);
        recvRemoteBase_ = reinterpret_cast<GM_ADDR>(context_->recvRemoteBufferAddr);

        pipe->InitBuffer(hcommBuf_, AscendC::HCOMM_UB_BUF_SIZE);
        hcommTensor_ = hcommBuf_.Get<uint8_t>();
        if (hcomm_.Init(hcommTensor_, AscendC::HCOMM_UB_BUF_SIZE) == AscendC::HCOMM_SUCCESS) {
            initOk_ = true;
        }
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        if (!initOk_) {
            context_->result = KERNEL_HCOMM_INIT_FAILED;
            return;
        }
        const uint32_t rounds = context_->rounds;
        const uint32_t apiMask = context_->apiMask;
        const uint32_t metricMask = context_->metricMask;
        const uint64_t len = context_->msgBytes;
        if (rounds == 0U || apiMask == 0U || (apiMask & ~API_ALL) != 0U || (metricMask & ~METRIC_ALL) != 0U ||
            len == 0U || (len & (sizeof(int32_t) - 1U)) != 0U) {
            context_->result = KERNEL_INVALID_ARGS;
            return;
        }

        // --api all 时一次 launch 依次测三接口；MODE_*（0/1/2）先换算成 apiMask 位再查掩码
        for (int mode = MODE_WRITE; mode <= MODE_NOTIFY; ++mode) {
            const uint32_t apiBit = (mode == MODE_WRITE) ? API_WRITE : (mode == MODE_READ) ? API_READ : API_NOTIFY;
            if ((apiMask & apiBit) == 0U) {
                continue;
            }
            bool ok = false;
            switch (mode) {
                case MODE_WRITE:
                    ok = context_->commitInline != 0U ? RunApi<true, MODE_WRITE>(rounds, metricMask, len) :
                                                        RunApi<false, MODE_WRITE>(rounds, metricMask, len);
                    break;
                case MODE_READ:
                    ok = context_->commitInline != 0U ? RunApi<true, MODE_READ>(rounds, metricMask, len) :
                                                        RunApi<false, MODE_READ>(rounds, metricMask, len);
                    break;
                default:
                    ok = context_->commitInline != 0U ? RunApi<true, MODE_NOTIFY>(rounds, metricMask, len) :
                                                        RunApi<false, MODE_NOTIFY>(rounds, metricMask, len);
                    break;
            }
            if (!ok) {
                return; // RunApi 已写 result / timing，终止后续接口
            }
        }
    }

private:
    // 一次发起：mode / COMMIT 都是模板参数，编译期分派。
    // write/notify 走 send 通道；read 走 recv 通道。
    // flagAddr 只有 notify 实例使用。
    template <bool COMMIT, int mode>
    __aicore__ inline int32_t Post(uint64_t len, GM_ADDR localAddr, GM_ADDR remoteAddr, GM_ADDR flagAddr)
    {
        if constexpr (mode == MODE_WRITE) {
            if constexpr (COMMIT) {
                return hcomm_.WriteNbi(channel_, remoteAddr, localAddr, len);
            }
            return hcomm_.WriteNbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(channel_, remoteAddr, localAddr, len);
        } else if constexpr (mode == MODE_READ) {
            if constexpr (COMMIT) {
                return hcomm_.ReadNbi(recvChannel_, localAddr, remoteAddr, len);
            }
            return hcomm_.ReadNbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(recvChannel_, localAddr, remoteAddr, len);
        } else {
            if constexpr (COMMIT) {
                return hcomm_.WriteWithNotifyNbi(channel_, remoteAddr, localAddr, len, flagAddr, NOTIFY_VAL_BASE);
            }
            return hcomm_.WriteWithNotifyNbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(
                channel_, remoteAddr, localAddr, len, flagAddr, NOTIFY_VAL_BASE);
        }
    }

    // warmup：固定 commit=true 逐条提交（不受 commit=false 的 sqDepth 约束），
    // 跑完 Drain 清干净。
    template <int mode>
    __aicore__ inline bool Warmup(
        uint32_t warmups, uint64_t len, GM_ADDR localFixed, GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep,
        uint64_t remoteStep)
    {
        for (uint32_t i = 0; i < warmups; ++i) {
            const int32_t ret =
                Post<true, mode>(len, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
            if (ret != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_WARMUP_FAILED;
                context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        const int32_t ret = hcomm_.Drain(mode == MODE_READ ? recvChannel_ : channel_);
        context_->drainRet = static_cast<uint32_t>(ret);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WARMUP_FAILED;
            context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        return true;
    }

    // 时延档：计时段内只发起，Commit（final 方式）与 Drain 都在段外
    template <bool COMMIT, int mode>
    __aicore__ inline bool RunLatency(
        uint32_t rounds, uint64_t len, GM_ADDR localFixed, GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep,
        uint64_t remoteStep)
    {
        int32_t retPost = AscendC::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t i = 0; i < rounds; ++i) {
            retPost = Post<COMMIT, mode>(len, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (retPost != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_LAT_POST_FAILED;
            context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retPost);
            return false;
        }
        int32_t ret = AscendC::HCOMM_SUCCESS;
        if constexpr (!COMMIT) {
            ret = hcomm_.Commit(mode == MODE_READ ? recvChannel_ : channel_);
            if (ret != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_LAT_COMMIT_FAILED;
                context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        ret = hcomm_.Drain(mode == MODE_READ ? recvChannel_ : channel_);
        context_->drainRet = static_cast<uint32_t>(ret);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_LAT_DRAIN_FAILED;
            context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        context_->timing[mode][LAT_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 带宽档：计时段内含收尾（Commit + Drain），端到端
    template <bool COMMIT, int mode>
    __aicore__ inline bool RunBandwidth(
        uint32_t rounds, uint64_t len, GM_ADDR localFixed, GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep,
        uint64_t remoteStep)
    {
        int32_t retPost = AscendC::HCOMM_SUCCESS;
        int32_t retCommit = AscendC::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t i = 0; i < rounds; ++i) {
            retPost = Post<COMMIT, mode>(len, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
        }
        if constexpr (!COMMIT) {
            retCommit = hcomm_.Commit(mode == MODE_READ ? recvChannel_ : channel_);
        }
        const int32_t ret = hcomm_.Drain(mode == MODE_READ ? recvChannel_ : channel_);
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (retPost != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_POST_FAILED;
            context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retPost);
            return false;
        }
        if (retCommit != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_COMMIT_FAILED;
            context_->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retCommit);
            return false;
        }
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_DRAIN_FAILED;
            context_->drainRet = static_cast<uint32_t>(ret);
            return false;
        }
        context_->timing[mode][BW_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 一个接口的完整测试：算地址 → warmup → 时延档 → 带宽档。
    // write/notify 落点在 send 对端；read 落点随轮次推进，源固定在 recv 对端。
    template <bool COMMIT, int mode>
    __aicore__ inline bool RunApi(uint32_t rounds, uint32_t metricMask, uint64_t len)
    {
        constexpr bool isRead = (mode == MODE_READ);
        GM_ADDR localFixed = isRead ? (localBase_ + READ_BASE) : localBase_;
        GM_ADDR remoteFixed =
            isRead ? recvRemoteBase_ : (remoteBase_ + ((mode == MODE_WRITE) ? WRITE_BASE : NOTIFY_BASE));
        const uint64_t localStep = isRead ? len : 0;
        const uint64_t remoteStep = isRead ? 0 : len;
        // notify flag 单槽；write/read 实例中折叠为常量 0
        GM_ADDR flagSlot = 0;
        if constexpr (mode == MODE_NOTIFY) {
            flagSlot = remoteBase_ + FLAG_BASE;
        }

        const uint32_t warmups = rounds < WARMUPS ? rounds : WARMUPS;
        if (!Warmup<mode>(warmups, len, localFixed, remoteFixed, flagSlot, localStep, remoteStep)) {
            return false;
        }
        if ((metricMask & METRIC_LAT) != 0U) {
            if (!RunLatency<COMMIT, mode>(rounds, len, localFixed, remoteFixed, flagSlot, localStep, remoteStep)) {
                return false;
            }
        }
        if ((metricMask & METRIC_BW) != 0U) {
            if (!RunBandwidth<COMMIT, mode>(rounds, len, localFixed, remoteFixed, flagSlot, localStep, remoteStep)) {
                return false;
            }
        }
        return true;
    }

    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    AscendC::LocalTensor<uint8_t> hcommTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    AscendC::ChannelHandle channel_{0};
    AscendC::ChannelHandle recvChannel_{0};
    GM_ADDR localBase_{nullptr};
    GM_ADDR remoteBase_{nullptr};
    GM_ADDR recvRemoteBase_{nullptr};
    bool initOk_{false};
};

} // namespace UrmaNormalApiPerfExample

extern "C" __vector__ __global__ __aicore__ void urma_normal_api_perf_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    UrmaNormalApiPerfExample::KernelUrmaNormalApiPerf op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchUrmaNormalApiPerf(void* stream, uint64_t context)
{
    urma_normal_api_perf_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
