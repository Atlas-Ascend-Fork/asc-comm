/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 *
 * Public SIMT Jetty entry point. The device implementation is shared from the
 * Jetty implementation tree while SIMD and SIMT public headers remain separate.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

#include "../../../impl/comm_api/aicore/jetty/impl/hcomm_jetty_simt_impl.h"

namespace AscendC::simt::jetty {

using HcommSge = JettySge;

/**
 * Public SIMT Jetty API. A call to Write publishes by default; use
 * kDoCommit=false for a group of posts followed by AdvanceSq and PublishSq.
 */
class HcommJetty {
public:
    static constexpr uint32_t kNumWQEBBLanes = JettyImpl::kNumWQEBBLanes;
    static constexpr uint32_t kNumWQEBBsPerSQESlot = JettyImpl::kNumWQEBBsPerSQESlot;
    static constexpr uint32_t kNumMaxSGEsPerWrite = JettyImpl::kNumMaxSGEsPerWrite;
    static constexpr uint32_t kNumWriteWqebbs = JettyImpl::kNumWriteWqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = JettyImpl::kNumWriteWithNotifyWqebbs;
    static constexpr uint32_t kNumMaxSQEBytes = JettyImpl::kNumMaxSQEBytes;
    static constexpr uint32_t kNumWQEBBsPerWrite = JettyImpl::kNumWQEBBsPerWrite;
    static constexpr uint32_t kNumDwqeWords = JettyImpl::kNumDwqeWords;

    __simt_callee__ __forceinline__ HcommJetty() = default;
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
        : impl_(info, stage, table, index) {}
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
        : impl_(info, stage, channelAddr) {}

    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const { return impl_.Info(); }

    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channel)
    {
        impl_.Attach(info, stage, channel);
    }

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop = Coop{}) const
    { return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, sges, coop); }

    // Cooperative-group-first spellings. The group leads the argument list to read like the
    // cooperative_groups API; a single SGE is passed by reference because the warp path only
    // consumes lane 0's descriptor.
    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Write(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    { return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, &sge, coop); }

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Write(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges) const
    { return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, sges, coop); }

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Put(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges, const Coop& coop = Coop{}) const
    { return Write<kSgeNum, kDoCommit>(peer, dst, sges, coop); }

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Put(
        const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
    { return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, &sge, coop); }
    template <int64_t kTimeout, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(
        const HcommPeer& peer, __gm__ void* dst, T value)
    { return impl_.template WriteValue<kTimeout, T, kDoCommit>(peer, dst, value); }
    template <int64_t kTimeout, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len,
        __gm__ void* notifyAddr, uint64_t notifyValue)
    { return impl_.template WriteWithNotify<kTimeout, kDoCommit>(peer, dst, src, len, notifyAddr, notifyValue); }

    __simt_callee__ __forceinline__ void AdvanceSq() const { impl_.AdvanceSq(); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop) const { impl_.AdvanceSq(coop); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(
        const Coop& coop, uint32_t a, uint32_t b) const { impl_.AdvanceSq(coop, a, b); }
    __simt_callee__ __forceinline__ void PublishSq() const { impl_.PublishSq(); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void PublishSq(const Coop& coop) const { impl_.PublishSq(coop); }

    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const { return impl_.template Drain<kTimeout>(); }
    template <int64_t kTimeout, typename Coop>
    __simt_callee__ __forceinline__ int32_t Drain(const Coop& coop) const
    { return impl_.template Drain<kTimeout>(coop); }

private:
    JettyImpl impl_;
};

} // namespace AscendC::simt::jetty

#endif
