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
 * \file asccomm_jetty_simt_impl.h
 * \brief Definitions of the public SIMT Jetty member functions.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_H

#include "asccomm_jetty_simt_impl_def.h"

// SIMT Jetty is only available on v310. The ASC driver compiles each translation unit twice:
// __NPU_ARCH__ is 3510, 9201, or 9202 in the device pass and undefined in the host pass. The public asccomm_jetty
// declares its members out-of-line, so definitions are only emitted on platforms that have the
// implementation; other platforms fail at link time if the API is used.
#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202
#include "platform_v310/asccomm_simt_urma_jetty.h"
#endif

namespace asc::simt {

#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202

__simt_callee__ __forceinline__ asccomm_jetty::asccomm_jetty() = default;

__simt_callee__ __forceinline__ asccomm_jetty::asccomm_jetty(
    __ubuf__ asccomm_jetty_info* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    : impl(info, stage, table, index)
{}

__simt_callee__ __forceinline__
asccomm_jetty::asccomm_jetty(__ubuf__ asccomm_jetty_info* info, __ubuf__ uint8_t* stage, uint64_t channel_addr)
    : impl(info, stage, channel_addr)
{}

__simt_callee__ __forceinline__ __ubuf__ asccomm_jetty_info* asccomm_jetty::info() const { return impl.info(); }

__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_attach(
    __ubuf__ asccomm_jetty_info* info, __ubuf__ uint8_t* stage, uint64_t channel)
{
    impl.asccomm_jetty_attach(info, stage, channel);
}

template <uint32_t sge_num, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_write_nbi(
    const asccomm_peer& peer, __gm__ void* dst, const jetty_sge* sges, const coop_group& group) const
{
    return impl.template asccomm_jetty_write_nbi<sge_num, do_commit>(peer, dst, sges, group);
}

template <uint32_t sge_num, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_write_nbi(
    const coop_group& group, const asccomm_peer& peer, __gm__ void* dst, const asccomm_sge& sge) const
{
    return impl.template asccomm_jetty_write_nbi<sge_num, do_commit>(peer, dst, &sge, group);
}

template <uint32_t sge_num, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_write_nbi(
    const coop_group& group, const asccomm_peer& peer, __gm__ void* dst, const asccomm_sge* sges) const
{
    return impl.template asccomm_jetty_write_nbi<sge_num, do_commit>(peer, dst, sges, group);
}

template <int64_t timeout_cycles, typename T, bool do_commit>
__simt_callee__ __forceinline__ int32_t
asccomm_jetty::asccomm_jetty_write_value_nbi(const asccomm_peer& peer, __gm__ void* dst, T value)
{
    return impl.template asccomm_jetty_write_value_nbi<timeout_cycles, T, do_commit>(peer, dst, value);
}

template <int64_t timeout_cycles, bool do_commit>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_write_with_notify_nbi(
    const asccomm_peer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_value)
{
    return impl.template asccomm_jetty_write_with_notify_nbi<timeout_cycles, do_commit>(
        peer, dst, src, len, notify_addr, notify_value);
}

__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_advance_sq() const
{
    impl.asccomm_jetty_advance_sq();
}

template <typename coop_group>
__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_advance_sq(const coop_group& group) const
{
    impl.asccomm_jetty_advance_sq(group);
}

template <typename coop_group>
__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_advance_sq(
    const coop_group& group, uint32_t a, uint32_t b) const
{
    impl.asccomm_jetty_advance_sq(group, a, b);
}

__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_publish_sq() const
{
    impl.asccomm_jetty_publish_sq();
}

template <typename coop_group>
__simt_callee__ __forceinline__ void asccomm_jetty::asccomm_jetty_publish_sq(const coop_group& group) const
{
    impl.asccomm_jetty_publish_sq(group);
}

template <int64_t timeout_cycles>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_drain() const
{
    return impl.template asccomm_jetty_drain<timeout_cycles>();
}

template <int64_t timeout_cycles, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_jetty::asccomm_jetty_drain(const coop_group& group) const
{
    return impl.template asccomm_jetty_drain<timeout_cycles>(group);
}

#endif // !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_SIMT_IMPL_H
