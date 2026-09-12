/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 *
 * Public SIMT Jetty entry point.  The device implementation lives under the
 * internal hcomm implementation tree so SIMD and SIMT public headers remain
 * separate while sharing the same UB metadata ABI.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

#include "../../../impl/comm_api/aicore/hcomm/impl/hcomm_simt_jetty_impl.h"

namespace AscendC::simt::jetty {

/**
 * Public SIMT Jetty API. A call to Write commits by default; use
 * kDoCommit=false for a group of posts followed by Commit or AdvanceSq.
 */
class HcommJetty {
public:
    static constexpr uint32_t kNumWQEBBLanes = HcommJettyImpl::kNumWQEBBLanes;
    static constexpr uint32_t kNumWQEBBsPerSQESlot = HcommJettyImpl::kNumWQEBBsPerSQESlot;
    static constexpr uint32_t kNumMaxSGEsPerWrite = HcommJettyImpl::kNumMaxSGEsPerWrite;
    static constexpr uint32_t kNumWriteWqebbs = HcommJettyImpl::kNumWriteWqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = HcommJettyImpl::kNumWriteWithNotifyWqebbs;
    static constexpr uint32_t kNumMaxSQEBytes = HcommJettyImpl::kNumMaxSQEBytes;
    static constexpr uint32_t kNumWQEBBsPerWrite = HcommJettyImpl::kNumWQEBBsPerWrite;
    static constexpr uint32_t kNumDwqeWords = HcommJettyImpl::kNumDwqeWords;

    __simt_callee__ __forceinline__ HcommJetty() = default;
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
        : impl_(info, stage, table, index) {}
    __simt_callee__ __forceinline__ HcommJetty(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
        : impl_(info, stage, channelAddr) {}

    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const { return impl_.Info(); }

    template <uint32_t kMaxSges, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges, const Coop& coop = Coop{}) const
    { return impl_.template Write<kMaxSges, kDoCommit>(peer, dst, sges, coop); }
    template <int64_t kTimeout, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t Write(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len)
    { return impl_.template Write<kTimeout, kDoCommit>(peer, dst, src, len); }
    template <int64_t kTimeout, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(
        const HcommPeer& peer, __gm__ void* dst, T value)
    { return impl_.template WriteValue<kTimeout, T, kDoCommit>(peer, dst, value); }
    template <int64_t kTimeout, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len,
        __gm__ void* notifyAddr, uint64_t notifyValue)
    { return impl_.template WriteWithNotify<kTimeout, kDoCommit>(peer, dst, src, len, notifyAddr, notifyValue); }

    template <typename Coop>
    __simt_callee__ __forceinline__ void Commit(const Coop& coop) const { impl_.Commit(coop); }
    __simt_callee__ __forceinline__ void Commit() const { impl_.Commit(); }
    __simt_callee__ __forceinline__ void AdvanceSq() const { impl_.AdvanceSq(); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop) const { impl_.AdvanceSq(coop); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(
        const Coop& coop, uint32_t a, uint32_t b) const { impl_.AdvanceSq(coop, a, b); }
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSqNoDoorbell(const Coop& coop) const
    { impl_.AdvanceSqNoDoorbell(coop); }
    __simt_callee__ __forceinline__ void PublishSq() const { impl_.PublishSq(); }

    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const { return impl_.template Drain<kTimeout>(); }
    template <int64_t kTimeout, typename Coop>
    __simt_callee__ __forceinline__ int32_t Drain(const Coop& coop) const
    { return impl_.template Drain<kTimeout>(coop); }

private:
    HcommJettyImpl impl_;
};

} // namespace AscendC::simt::jetty

#endif
