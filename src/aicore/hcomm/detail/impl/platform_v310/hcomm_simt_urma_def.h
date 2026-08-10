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
 * \file hcomm_simt_urma_def.h
 * \brief Hcomm SIMT URMA definition
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H

#include "../../common/hcomm_simt_inner_def.h"

namespace AscendC::simt {

// SIMT rings the doorbell by copying a 128B DWQE to dbVa - 0x80; the scalar register write the
// SIMD path uses is not available here.
constexpr uint32_t HCOMM_URMA_DWQE_DB_OFFSET = 0x80U;
constexpr uint32_t HCOMM_URMA_DWQE_SIZE = 128U;
constexpr uint32_t HCOMM_URMA_WQE_BB_SIZE = 64U;
constexpr uint32_t HCOMM_URMA_WQE_BB_CNT = 1U;
constexpr uint32_t HCOMM_URMA_DWQE_BB_CNT = HCOMM_URMA_DWQE_SIZE / HCOMM_URMA_WQE_BB_SIZE;
constexpr uint32_t HCOMM_SIMT_MAX_CQ_RETRY = 1000000U;

template <>
class HcommImpl<COMM_PROTOCOL_UBC_CTP> {
public:
    __simt_callee__ inline HcommImpl();
    __simt_callee__ inline ~HcommImpl();
    __simt_callee__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);
    __simt_callee__ inline int32_t ReserveDwqeBatch(
        ChannelHandle channel, __ubuf__ uint32_t* state, bool active,
        uint32_t activeWqeCnt, uint32_t commitLane);
    template <bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <
        typename T, bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value);
    template <bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t ReadNbi(ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t WriteWithNotifyNbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyVal);
    template <
        typename T, bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicFAA(ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal);
    template <
        typename T, bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t AtomicCAS(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
    template <auto pipe = 0>
    __simt_callee__ inline int32_t Drain(ChannelHandle channel);

private:
    bool hasReservedDwqeSlot_ = false;
    uint32_t reservedDwqeHead_ = 0U;
    bool hasDeferredDwqeBatch_ = false;
    uint32_t deferredDwqeBbCnt_ = 0U;
    uint32_t deferredDwqeCqeCnt_ = 0U;
    template <
        bool commit = true, auto commitPipe = 0, auto reqPipe = 0, HcommUrmaOpCode opCode = HcommUrmaOpCode::WRITE,
        auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t PostSend(
        ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len);
    template <bool commit = true, auto commitPipe = 0, auto reqPipe = 0, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t PostWriteWithNotify(
        ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
        __gm__ uint8_t* notifyAddr, uint64_t notifyVal);
    template <
        typename T, bool commit = true, auto commitPipe = 0, auto reqPipe = 0,
        HcommUrmaOpCode opCode = HcommUrmaOpCode::FAA, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t PostAtomic(
        ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond);
    __simt_callee__ inline int32_t PublishDwqeBatch(__gm__ uint64_t* headAddr);
    __simt_callee__ inline uint32_t PollCq(ChannelHandle channel, uint32_t expectIdx);
};

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif
