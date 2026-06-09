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
 * \file hcomm_aiv_urma_def.h
 * \brief Hcomm AIV URMA definition for V310
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv_urma_def.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {

constexpr uint32_t HCOMM_URMA_MAX_RETRY_TIMES = 1000000;
constexpr uint32_t HCOMM_URMA_DEFAULT_QP_IDX = 0;
constexpr uint32_t HCOMM_URMA_TMP_BUF_SIZE = 512;
constexpr uint32_t HCOMM_URMA_WQE_U32_NUM = 32;
constexpr uint32_t HCOMM_URMA_CQE_U32_NUM = 16;

enum class HcommUrmaOpCode : uint32_t {
    SEND = 0U,
    SEND_WITH_IMM,
    SEND_WITH_INV,
    WRITE,
    WRITE_WITH_IMM,
    WRITE_WITH_NOTIFY,
    READ,
    CAS,
    ATOMIC_SWAP,
    ATOMIC_STORE,
    ATOMIC_LOAD,
    FAA = 0xBU,
    WRITE_WITH_REDUCE = 0x10U,
    NOP = 0x11U,
};

template <>
class HcommImpl<COMM_PROTOCOL_UBC_CTP> {
public:
    __aicore__ inline HcommImpl();
    __aicore__ inline ~HcommImpl();
    __aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename T>
    __aicore__ inline int32_t Init(const LocalTensor<T>& buff, uint32_t len);
    template <bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const &config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const &config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t ReadNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        auto const &config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t WriteWithNotifyNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len,
        GM_ADDR notifyAddr, uint64_t notifyVal);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t Commit(ChannelHandle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Drain(ChannelHandle channel);

private:
    template <bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3,
        HcommUrmaOpCode opCode = HcommUrmaOpCode::WRITE, auto const &config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t PostSend(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr = nullptr,
        uint64_t notifyVal = 0);
    __aicore__ inline void PollCqWhenSqOverflow(
        ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead);
    __aicore__ inline uint32_t PollCq(ChannelHandle channel, uint32_t expectTail);

private:
    LocalTensor<uint32_t> wqeItem_;
    LocalTensor<uint32_t> cqeItem_;
    LocalTensor<uint32_t> sqPI_;
    LocalTensor<uint32_t> sqCI_;
    LocalTensor<uint32_t> cqCI_;
};
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H__
#endif
