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
 * \file hcomm_jetty_inner_def.h
 * \brief Jetty metadata shared by the SIMD and SIMT implementations.
 *
 * Both execution models describe the same hardware queues and exchange these
 * descriptors through UB, so the layouts must stay bit-identical. Keeping one
 * definition here makes that guarantee structural instead of relying on two
 * copies being edited together.
 */

#ifndef IMPL_ADV_API_DETAIL_JETTY_COMMON_HCOMM_JETTY_INNER_DEF_H
#define IMPL_ADV_API_DETAIL_JETTY_COMMON_HCOMM_JETTY_INNER_DEF_H

#include <cstddef>
#include <cstdint>

namespace AscendC {

struct alignas(8) HcommJettyPeerInfo {
    // Packed to match the SQE wire layout so building a WQE header is a single 64-bit read:
    // tpId in bits [23:0], numSges in bits [31:24] (filled per-post, not stored here) and
    // remoteTokenId in bits [51:32].
    uint64_t tpIdNumSgesRemoteTokenId;
    uint64_t remoteEid[2];
    uint64_t remoteTokenValueUdf;
    uint64_t remoteBaseAddr;
    uint64_t remoteBufferSize;
};
static_assert(sizeof(HcommJettyPeerInfo) == 48U, "HcommJettyPeerInfo must be 48 bytes");
static_assert(offsetof(HcommJettyPeerInfo, remoteEid) == sizeof(uint64_t), "unexpected peer metadata layout");
static_assert(offsetof(HcommJettyPeerInfo, remoteTokenValueUdf) == 3U * sizeof(uint64_t),
    "unexpected peer metadata layout");

struct alignas(8) HcommJettyInfo {
    uint64_t sqBaseAddr;         // SQ base address in device memory.
    uint64_t sqHeadAddr;         // Address of the packed 64-bit SQ head in GM.
    uint64_t completionTailAddr; // Address of the 32-bit SQ completion tail published by the hardware.
    uint64_t sqDoorbellAddr;     // SQ doorbell address.
    uint64_t cqBaseAddr;         // CQ base address in device memory.
    uint64_t cqTailAddr;         // Address of the 32-bit CQ tail.
    uint64_t cqDoorbellAddr;     // CQ doorbell address.
    uint64_t packedHead;         // Producer state mirroring the GM SQ head: low 32 bits are the head
                                 // in WQEBBs, high 32 bits the expected CQE count. Kept packed so
                                 // publishing is a single 64-bit store.
    uint32_t numWqebbBytes;      // WQEBB size in bytes.
    uint32_t numCqeBytes;        // CQE size in bytes.
    uint32_t sqDepth;            // SQ depth in WQEBBs.
    uint32_t cqDepth;            // CQ depth in CQEs.
};
static_assert(sizeof(HcommJettyInfo) == 80U, "HcommJettyInfo must be 80 bytes");
static_assert(offsetof(HcommJettyInfo, packedHead) == 56U, "SIMT/SIMD packed head offset mismatch");
static_assert(offsetof(HcommJettyInfo, numWqebbBytes) == 64U, "HcommJettyInfo scalar fields offset mismatch");

} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_JETTY_COMMON_HCOMM_JETTY_INNER_DEF_H
