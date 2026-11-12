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
 * \file urma_batch_api_perf_kernel.cpp
 * \brief Device side of the URMA batch API performance example.
 */

#include "kernel_operator.h"
#include "hcomm/hcomm.h"

#include "urma_batch_api_perf_def.h"

namespace UrmaBatchApiPerfExample {

// 批内 WQE 模板入参：只有每批的末条请求用 cqe=1
constexpr AscendC::UrmaWqeEntry WQE_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};
constexpr AscendC::UrmaWqeEntry WQE_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inlineEn = 0,
};

// OP_* 同时是 timing 第一维槽位（与 def.h 的 API_*_IDX 同值），
// static_assert 编译期钉死这个 ABI。
enum TestOperation : int32_t {
    OP_WRITE = 0, // WriteNbi
    OP_READ = 1,  // ReadNbi
    OP_NOTIFY = 2 // WriteWithNotifyNbi
};

static_assert(
    OP_WRITE == static_cast<int32_t>(API_WRITE_IDX) && OP_READ == static_cast<int32_t>(API_READ_IDX) &&
        OP_NOTIFY == static_cast<int32_t>(API_NOTIFY_IDX),
    "OP_* values must match API_*_IDX timing slots");

class KernelUrmaBatchApiPerf {
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
        pipe_ = pipe;
        initOk_ = true;
    }

    __aicore__ inline void Process()
    {
        context_->result = KERNEL_SUCCESS;
        if (!initOk_) {
            context_->result = KERNEL_HCOMM_INIT_FAILED;
            return;
        }
        const uint32_t rounds = context_->rounds;
        const uint32_t batchCap = context_->batchCap;
        const uint32_t apiMask = context_->apiMask;
        const uint32_t metricMask = context_->metricMask;
        const uint64_t len = context_->msgBytes;
        if (rounds == 0U || batchCap == 0U || apiMask == 0U || (apiMask & ~API_ALL) != 0U ||
            (metricMask & ~METRIC_ALL) != 0U || len == 0U || (len & (sizeof(int32_t) - 1U)) != 0U) {
            context_->result = KERNEL_INVALID_ARGS;
            return;
        }
        const CaseLayout layout = CaseLayoutOf(len, batchCap);
        if (layout.totalBytes > BUFFER_BYTES) {
            context_->result = KERNEL_BUFFER_OVERFLOW;
            return;
        }

        // batch 暂存缓冲大小（每条 WQEBB 占用取三操作里最大的 notify），并校验 UB 预算
        const uint32_t maxWqebbPerOp = (apiMask & API_NOTIFY) ? NOTIFY_WQEBB : 1U;
        const uint32_t batchBufBytes = batchCap * maxWqebbPerOp * WQEBB_BYTES;
        if (batchBufBytes > BATCH_UB_BUDGET_BYTES) {
            context_->result = KERNEL_UB_OVERFLOW;
            return;
        }
        pipe_->InitBuffer(batchBuffer_, batchBufBytes);
        batchTensor_ = batchBuffer_.Get<uint8_t>();

        // --api all 时一次 launch 依次测三操作（OP_* 先换算成 apiMask 位再查掩码）
        for (int op = OP_WRITE; op <= OP_NOTIFY; ++op) {
            const uint32_t apiBit = (op == OP_WRITE) ? API_WRITE : (op == OP_READ) ? API_READ : API_NOTIFY;
            if ((apiMask & apiBit) == 0U) {
                continue;
            }
            bool ok = false;
            switch (op) {
                case OP_WRITE:
                    ok = RunOp<OP_WRITE>(layout, rounds, batchCap, batchBufBytes, len);
                    break;
                case OP_READ:
                    ok = RunOp<OP_READ>(layout, rounds, batchCap, batchBufBytes, len);
                    break;
                default:
                    ok = RunOp<OP_NOTIFY>(layout, rounds, batchCap, batchBufBytes, len);
                    break;
            }
            if (!ok) {
                return; // RunOp 已写 result / timing，终止后续操作
            }
        }
    }

private:
    // 往 batchHandle 攒一条 WQE；op / CQE 都是模板参数，编译期分派。
    // write/notify 走 send 通道；read 走 recv 通道。flagAddr 只有 notify 实例使用。
    template <int op, bool enableCqe>
    __aicore__ inline int32_t PostBatch(
        AscendC::UbcBatchHandle& batchHandle, uint32_t len, GM_ADDR localAddr, GM_ADDR remoteAddr, GM_ADDR flagAddr)
    {
        if constexpr (op == OP_WRITE) {
            if constexpr (enableCqe) {
                return hcomm_.WriteNbi<WQE_CQE_CFG>(batchHandle, remoteAddr, localAddr, len);
            }
            return hcomm_.WriteNbi<WQE_NO_CQE_CFG>(batchHandle, remoteAddr, localAddr, len);
        } else if constexpr (op == OP_READ) {
            if constexpr (enableCqe) {
                return hcomm_.ReadNbi<WQE_CQE_CFG>(batchHandle, localAddr, remoteAddr, len);
            }
            return hcomm_.ReadNbi<WQE_NO_CQE_CFG>(batchHandle, localAddr, remoteAddr, len);
        } else {
            if constexpr (enableCqe) {
                return hcomm_.WriteWithNotifyNbi<WQE_CQE_CFG>(
                    batchHandle, remoteAddr, localAddr, len, flagAddr, NOTIFY_VAL_BASE);
            }
            return hcomm_.WriteWithNotifyNbi<WQE_NO_CQE_CFG>(
                batchHandle, remoteAddr, localAddr, len, flagAddr, NOTIFY_VAL_BASE);
        }
    }

    // warmup：固定 WARMUP_ROUNDS(=1) 个完整批轮次（B 条 → BatchCommit → Drain）
    template <int op>
    __aicore__ inline bool Warmup(
        AscendC::UbcBatchHandle& batchHandle, uint32_t lastBatchIndex, uint32_t msgLen, GM_ADDR localFixed,
        GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep, uint64_t remoteStep)
    {
        for (uint32_t i = 0; i < lastBatchIndex; ++i) {
            const int32_t ret = PostBatch<op, false>(
                batchHandle, msgLen, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
            if (ret != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_WARMUP_FAILED;
                context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        int32_t ret = PostBatch<op, true>(
            batchHandle, msgLen, localFixed + lastBatchIndex * localStep, remoteAddr + lastBatchIndex * remoteStep,
            flagSlot);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WARMUP_FAILED;
            context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        ret = hcomm_.BatchCommit(batchHandle);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WARMUP_FAILED;
            context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        ret = hcomm_.Drain(batchHandle);
        context_->drainRet = static_cast<uint32_t>(ret);
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_WARMUP_FAILED;
            context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        return true;
    }

    // 时延档：逐轮分段计时，Drain 在段外，BatchCommit 依 COMMIT_IN_WINDOW
    // 计入或挪到段外（模板参数，热循环内零分支）
    template <int op, bool COMMIT_IN_WINDOW>
    __aicore__ inline bool RunLatency(
        AscendC::UbcBatchHandle& batchHandle, uint32_t rounds, uint32_t lastBatchIndex, uint32_t msgLen,
        GM_ADDR localFixed, GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep, uint64_t remoteStep)
    {
        uint64_t elapsed = 0;
        int32_t retPost = AscendC::HCOMM_SUCCESS;
        int32_t retCommit = AscendC::HCOMM_SUCCESS;
        for (uint32_t r = 0; r < rounds; ++r) {
            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t t0 = AscendC::GetSystemCycle();
            for (uint32_t i = 0; i < lastBatchIndex; ++i) {
                retPost = PostBatch<op, false>(
                    batchHandle, msgLen, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
            }
            retPost = PostBatch<op, true>(
                batchHandle, msgLen, localFixed + lastBatchIndex * localStep, remoteAddr + lastBatchIndex * remoteStep,
                flagSlot);
            if constexpr (COMMIT_IN_WINDOW) {
                retCommit = hcomm_.BatchCommit(batchHandle);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t t1 = AscendC::GetSystemCycle();
            elapsed += t1 - t0;
            // 计时段结束（以下出窗）
            if (retPost != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_LAT_POST_FAILED;
                context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retPost);
                return false;
            }
            if (retCommit != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_LAT_COMMIT_FAILED;
                context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retCommit);
                return false;
            }
            // 段外收尾：commit=false 时补发 BatchCommit，再 Drain 清空 SQ 进下一轮
            int32_t ret = AscendC::HCOMM_SUCCESS;
            if constexpr (!COMMIT_IN_WINDOW) {
                ret = hcomm_.BatchCommit(batchHandle);
                if (ret != AscendC::HCOMM_SUCCESS) {
                    context_->result = KERNEL_LAT_COMMIT_FAILED;
                    context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                    return false;
                }
            }
            ret = hcomm_.Drain(batchHandle);
            context_->drainRet = static_cast<uint32_t>(ret);
            if (ret != AscendC::HCOMM_SUCCESS) {
                context_->result = KERNEL_LAT_DRAIN_FAILED;
                context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        context_->timing[op][LAT_CYCLES_INDEX] = elapsed;
        return true;
    }

    // 带宽档：单窗口包住全部轮次 + 每批 BatchCommit + 末尾 Drain，端到端
    template <int op>
    __aicore__ inline bool RunBandwidth(
        AscendC::UbcBatchHandle& batchHandle, uint32_t rounds, uint32_t lastBatchIndex, uint32_t msgLen,
        GM_ADDR localFixed, GM_ADDR remoteAddr, GM_ADDR flagSlot, uint64_t localStep, uint64_t remoteStep)
    {
        int32_t retPost = AscendC::HCOMM_SUCCESS;
        int32_t retCommit = AscendC::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t r = 0; r < rounds; ++r) {
            for (uint32_t i = 0; i < lastBatchIndex; ++i) {
                retPost = PostBatch<op, false>(
                    batchHandle, msgLen, localFixed + i * localStep, remoteAddr + i * remoteStep, flagSlot);
            }
            retPost = PostBatch<op, true>(
                batchHandle, msgLen, localFixed + lastBatchIndex * localStep, remoteAddr + lastBatchIndex * remoteStep,
                flagSlot);
            retCommit = hcomm_.BatchCommit(batchHandle);
        }
        int32_t ret = hcomm_.Drain(batchHandle);
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (retPost != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_POST_FAILED;
            context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retPost);
            return false;
        }
        if (retCommit != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_COMMIT_FAILED;
            context_->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(retCommit);
            return false;
        }
        if (ret != AscendC::HCOMM_SUCCESS) {
            context_->result = KERNEL_BW_DRAIN_FAILED;
            context_->drainRet = static_cast<uint32_t>(ret);
            return false;
        }
        context_->timing[op][BW_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 单个操作的全流程：算地址 → MakeBatchHandle → warmup → 时延档 → 带宽档。
    // write/notify 落点在 send 对端；read 落点按批内 index 推进，源固定在 recv 对端。
    template <int op>
    __aicore__ inline bool RunOp(
        const CaseLayout& layout, uint32_t rounds, uint32_t batchCap, uint32_t batchBufBytes, uint64_t msgBytes)
    {
        constexpr bool isRead = (op == OP_READ);
        const uint32_t msgLen = static_cast<uint32_t>(msgBytes);

        GM_ADDR localFixed = isRead ? (localBase_ + layout.readBase) : localBase_;
        GM_ADDR remoteFixed =
            isRead ? recvRemoteBase_ : (remoteBase_ + ((op == OP_WRITE) ? layout.writeBase : layout.notifyBase));
        const uint64_t localStep = isRead ? msgLen : 0;
        const uint64_t remoteStep = isRead ? 0 : msgLen;
        // notify flag 单槽，地址窗外算好；write/read 实例中折叠为常量 0
        GM_ADDR flagSlot = 0;
        if constexpr (op == OP_NOTIFY) {
            flagSlot = remoteBase_ + layout.flagsBase;
        }
        const uint32_t lastBatchIndex = batchCap - 1U;

        // 批句柄绑定单条通道：read 用 recv 通道，其余用 send 通道
        AscendC::ChannelHandle channel = isRead ? recvChannel_ : channel_;
        auto batchHandle = hcomm_.MakeBatchHandle(channel, batchTensor_, batchBufBytes, remoteFixed, localFixed);
        if (batchHandle.channelHandle == 0U) {
            context_->result = KERNEL_MAKE_HANDLE_FAILED;
            return false;
        }

        if (!Warmup<op>(
                batchHandle, lastBatchIndex, msgLen, localFixed, remoteFixed, flagSlot, localStep, remoteStep)) {
            return false;
        }
        if ((context_->metricMask & METRIC_LAT) != 0U) {
            // commitInWindow 读一次选进模板参数，热循环内零分支
            const bool latOk = context_->commitInWindow != 0U ?
                                   RunLatency<op, true>(
                                       batchHandle, rounds, lastBatchIndex, msgLen, localFixed, remoteFixed, flagSlot,
                                       localStep, remoteStep) :
                                   RunLatency<op, false>(
                                       batchHandle, rounds, lastBatchIndex, msgLen, localFixed, remoteFixed, flagSlot,
                                       localStep, remoteStep);
            if (!latOk) {
                return false;
            }
        }
        if ((context_->metricMask & METRIC_BW) != 0U) {
            if (!RunBandwidth<op>(
                    batchHandle, rounds, lastBatchIndex, msgLen, localFixed, remoteFixed, flagSlot, localStep,
                    remoteStep)) {
                return false;
            }
        }
        return true;
    }

    AscendC::TPipe* pipe_{nullptr};
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer_;
    AscendC::LocalTensor<uint8_t> batchTensor_;
    HcommType hcomm_;
    __gm__ KernelContext* context_{nullptr};
    AscendC::ChannelHandle channel_{0};
    AscendC::ChannelHandle recvChannel_{0};
    GM_ADDR localBase_{nullptr};
    GM_ADDR remoteBase_{nullptr};
    GM_ADDR recvRemoteBase_{nullptr};
    bool initOk_{false};
};

} // namespace UrmaBatchApiPerfExample

extern "C" __vector__ __global__ __aicore__ void urma_batch_api_perf_kernel(GM_ADDR context)
{
    AscendC::TPipe pipe;
    UrmaBatchApiPerfExample::KernelUrmaBatchApiPerf op;
    op.Init(context, &pipe);
    op.Process();
}

void LaunchUrmaBatchApiPerf(void* stream, uint64_t context)
{
    urma_batch_api_perf_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
