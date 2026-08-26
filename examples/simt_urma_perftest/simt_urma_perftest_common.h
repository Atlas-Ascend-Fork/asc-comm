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
 * \file simt_urma_perftest_common.h
 * \brief Constants shared by the SIMT URMA performance example host and device code
 */

#ifndef EXAMPLES_SIMT_URMA_PERFTEST_COMMON_H
#define EXAMPLES_SIMT_URMA_PERFTEST_COMMON_H

#include <cstdint>

namespace simt_urma_perftest {

constexpr int kSenderRank = 0;
constexpr int kReceiverRank = 1;
// 点对点性能测试只使用前两个rank：只有一个发送方计时，多方并发发送会互相争抢资源并污染测量。
// 允许拉起更多进程，其余rank直接退出，不参与建链和性能测试。
constexpr int kBenchRanks = 2;

// Byte the sender fills its payload with, and the value WriteValueNbi carries inline. Both are
// distinctive enough that a receive buffer left at its initial 0 is obvious.
constexpr uint8_t kPayloadByte = 0x5AU;
constexpr uint64_t kInlineValue = 0x1122334455667788ULL;

// Notify/atomic operands. The atomics run unchecked across iterations -- FAA accumulates and CAS
// only swaps on the first iteration -- so these exist to give the WQE well-formed operands
// rather than to be verified afterwards.
constexpr uint64_t kNotifySignal = 0x2222222222222222ULL;
constexpr uint64_t kFaaAdd = 3U;
constexpr uint64_t kCasInitial = 20U;
constexpr uint64_t kCasSwap = 30U;

// WriteNbi with commit=true splits its payload across two SGEs so the WQE fills the 128-byte
// DWQE window that publishes it. A one-byte payload would give the first SGE a zero length,
// which URMA does not accept, so the smallest payload this example allows is two bytes.
constexpr uint32_t kMinPayloadBytes = 2U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kDrainStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kTimingWords = 4U;

// NOTIFY 在 payload 之后还会写一个远端 signal word，因此它的 slot 占用比 payload 本身大。
constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiWriteValue = 1U;
constexpr uint32_t kApiNotify = 2U;
constexpr uint32_t kApiFaa = 3U;
constexpr uint32_t kApiCas = 4U;

constexpr bool ApiUsesPayload(uint32_t api) { return api == kApiWrite || api == kApiNotify; }

// 另外三个不读发送缓冲区：WRITE_VALUE 的 payload 内联在 WQE 里，两个原子接口只回写取回值。
constexpr bool ApiReadsSendBuffer(uint32_t api) { return api == kApiWrite || api == kApiNotify; }

// 只有这两个接口的落地镜像与迭代次数无关：NOTIFY 的 signal word 与 payload 共用 slot，两个原子
// 接口每次迭代都改写目标 word，都没有稳定的期望值可比。
constexpr bool ApiHasStableReceiverImage(uint32_t api) { return api == kApiWrite || api == kApiWriteValue; }

} // namespace simt_urma_perftest

#endif // EXAMPLES_SIMT_URMA_PERFTEST_COMMON_H
