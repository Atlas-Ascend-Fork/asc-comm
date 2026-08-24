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

// Which interface a run exercises.
//   WRITE        WQE points at a local buffer through an SGE; payload length is configurable.
//   WRITE_VALUE  8-byte payload carried inline in the WQE, no SGE and no local read.
//   NOTIFY       WriteNbi plus a remote signal word written after the payload.
//   FAA / CAS    8-byte atomics against a remote word.
constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiWriteValue = 1U;
constexpr uint32_t kApiNotify = 2U;
constexpr uint32_t kApiFaa = 3U;
constexpr uint32_t kApiCas = 4U;

// Only WRITE and NOTIFY move a configurable payload; the other three always move 8 bytes.
constexpr bool ApiUsesPayload(uint32_t api) { return api == kApiWrite || api == kApiNotify; }

// WRITE and NOTIFY read the local send buffer through an SGE. The others do not: WRITE_VALUE
// carries its payload inline, and the atomics only write a fetched value back.
constexpr bool ApiReadsSendBuffer(uint32_t api) { return api == kApiWrite || api == kApiNotify; }

// The receiver can only be checked for the two plain data movers. NOTIFY also lands its payload,
// but its signal word shares the slot, and the atomics mutate their target every iteration, so
// neither has a stable expected image after an arbitrary iteration count.
constexpr bool ApiHasStableReceiverImage(uint32_t api) { return api == kApiWrite || api == kApiWriteValue; }

} // namespace simt_urma_perftest

#endif // EXAMPLES_SIMT_URMA_PERFTEST_COMMON_H
