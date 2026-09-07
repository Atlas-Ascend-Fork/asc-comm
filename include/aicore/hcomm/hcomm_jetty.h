/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms of the
 * CANN Open Software License Agreement Version 2.0.
 */

/*!
 * \file hcomm_jetty.h
 * \brief Low-level AIV Jetty primitives for URMA point-to-point writes.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H

#include "kernel_basic_intf.h"
#include "hcomm_common.h"

namespace AscendC {

constexpr uint32_t HCOMM_JETTY_LAST_REMOTE_BUFFER = 0xFFFFFFFFU;

struct alignas(8) HcommJettyPeerInfo {
    uint64_t tpIdNumSgesRemoteTokenId;
    uint64_t remoteEid[2];
    uint64_t remoteTokenValueUdf;
    uint64_t remoteBaseAddr;
    uint64_t remoteBufferSize;
};
static_assert(sizeof(HcommJettyPeerInfo) == 48U, "HcommJettyPeerInfo must be 48 bytes");

struct alignas(8) HcommJettyInfo {
    uint64_t sqBaseAddr;
    uint64_t sqHeadAddr;
    uint64_t completionTailAddr;
    uint64_t sqDoorbellAddr;
    uint64_t cqBaseAddr;
    uint64_t cqTailAddr;
    uint64_t cqDoorbellAddr;
    uint64_t packedHead;
    uint32_t numWqebbBytes;
    uint32_t numCqeBytes;
    uint32_t sqDepth;
    uint32_t cqDepth;
};
static_assert(sizeof(HcommJettyInfo) == 80U, "HcommJettyInfo must be 80 bytes");

struct HcommPeer {
    __ubuf__ HcommJettyPeerInfo* peerInfo = nullptr;
    bool valid = false;

    __aicore__ __forceinline__ HcommPeer(
        __ubuf__ HcommJettyPeerInfo* peerInfo, __gm__ void* jettyTable, uint32_t jettyIdx,
        uint32_t remoteBufferIdx = HCOMM_JETTY_LAST_REMOTE_BUFFER);
};

} // namespace AscendC

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H
#endif

#if defined(ASCCOMM_BUILD_FROM_SOURCE)
#include "../../../src/aicore/hcomm/impl/hcomm_jetty_impl_def.h"
#else
#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_jetty_impl_def.h"
#endif

namespace AscendC {

class HcommJetty {
public:
    static constexpr uint32_t kNumWriteWqebbs = 1U;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = 2U;
    static constexpr uint32_t kNumMaxSqeBytes = kNumWriteWithNotifyWqebbs * 64U;

    __aicore__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
        __gm__ void* jettyTable, uint32_t jettyIdx);

    // Set kDoCommit=false to enqueue the WQE without ringing the SQ doorbell.
    // A later Write<..., true> publishes the accumulated SQ entries.
    // dstPtr is the externally supplied remote device address.
    template <int64_t kNumTimeoutCycles, bool kDoCommit = true>
    __aicore__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dstPtr, __gm__ void* srcPtr, uint64_t numBytes);

    // WriteValue carries value inline in a single WQEBB; T must fit after the SQE header.
    template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit = true>
    __aicore__ __forceinline__ int32_t WriteValue(
        const HcommPeer& peer, __gm__ void* dstPtr, T value);

    // dstPtr and notifyAddr are externally supplied remote device addresses.
    template <int64_t kNumTimeoutCycles>
    __aicore__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dstPtr, __gm__ void* srcPtr, uint64_t numBytes,
        __gm__ void* notifyAddr, uint64_t notifyValue);

    template <int64_t kNumTimeoutCycles>
    __aicore__ __forceinline__ int32_t Drain();

private:
    HcommJettyImpl impl_;
};

} // namespace AscendC

#if defined(ASCCOMM_BUILD_FROM_SOURCE)
#include "../../../src/aicore/hcomm/impl/hcomm_jetty_impl.h"
#else
#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_jetty_impl.h"
#endif

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H
#endif

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
