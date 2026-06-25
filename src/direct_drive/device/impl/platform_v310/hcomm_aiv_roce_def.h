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
 * \file hcomm_aiv_roce_def.h
 * \brief Hcomm AIV RoCE definition for V310
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv_roce_def.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {
constexpr uint32_t ROCE_SQ_DOORBELL_TYPE = 2;
constexpr uint32_t ROCE_INIT_SQ_DB_SGIT_IDX = 1;
constexpr uint32_t ROCE_CQE_POS = 128;
constexpr uint32_t ROCE_DB_POS = 192;
enum class HCOMM_ROCE_OP_TYPE : uint32_t { WRITE = 4U, READ = 8U };

template <>
class HcommImpl<COMM_PROTOCOL_ROCE> {
public:
    __aicore__ inline HcommImpl() {}
    __aicore__ inline ~HcommImpl() {}
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
    template <bool commit = true, pipe_t commitPipe = PIPE_S, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline int32_t PostSend(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType);
    template <pipe_t pipe>
    __aicore__ inline void KnockDoorBell(__gm__ ChannelEntity* chnlPtr, uint32_t sqHead);
    __aicore__ inline int32_t PollCq(__gm__ ChannelEntity* chnlPtr, uint32_t expectIdx);
    __aicore__ inline int32_t MakeWqe(__gm__ ChannelEntity* chnlPtr, GM_ADDR dst, GM_ADDR src, uint64_t len,
        uint32_t opType, uint32_t sqHead, uint32_t sqDepth);
    __aicore__ inline uint64_t GetDbValue(uint32_t qpn);

private:
    __ubuf__ uint8_t *wqeAddr_;
    __ubuf__ uint8_t *cqeAddr_;
    __ubuf__ uint8_t *dbAddr_;
    LocalTensor<uint8_t> wqeUB_;
    LocalTensor<uint8_t> cqeUB_;
    LocalTensor<uint8_t> dbUB_;
};
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H__
#endif
