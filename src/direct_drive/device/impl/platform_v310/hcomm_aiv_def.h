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
 * \file hcomm_aiv_def.h
 * \brief Hcomm AIV definition for V310
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv_def.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_DEF_H

namespace AscendC {
constexpr int8_t HCOMM_INVALID_HANDLE_ID = -1;
constexpr uint32_t HCOMM_MAX_HANDLE_ID = 1024;
constexpr uint32_t HCOMM_TMP_BUF_SIZE = 224;
constexpr uint32_t ROCE_SQ_DOORBELL_TYPE = 2;
constexpr uint32_t ROCE_INIT_SQ_DB_SGIT_IDX = 1;

enum class HCOMM_OP_TYPE : uint32_t { WRITE = 4U, READ = 8U };

template <>
class HcommImpl<CommEngine::AIV, CommProtocol::ROCE> {
public:
    __aicore__ inline HcommImpl();
    __aicore__ inline ~HcommImpl();
    template <bool commit = true, pipe_t commitPipe = PIPE_MTE3, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline HcommHandle Write(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <bool commit = true, pipe_t commitPipe = PIPE_MTE3, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline HcommHandle Read(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Commit(HcommHandle handleId);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Wait(HcommHandle handleId);

private:
    template <bool commit = true, pipe_t commitPipe = PIPE_MTE3, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline HcommHandle Operate(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType);
    template <bool isWait>
    __aicore__ inline bool JudgeHandleId(HcommHandle handleId);

private:
    TPipe pipe_;
    TBuf<TPosition::VECOUT> hcommBuf_;
    LocalTensor<uint32_t> wqeItem_;
    LocalTensor<uint32_t> cqeItem_;
    LocalTensor<uint32_t> sqPI_;
    LocalTensor<uint32_t> sqCI_;
    LocalTensor<uint32_t> cqCI_;
    LocalTensor<uint32_t> doorBell_;
    ChannelHandle channelList_[HCOMM_MAX_HANDLE_ID] = {0};
    bool handleCommitList_[HCOMM_MAX_HANDLE_ID] = {0};
    bool handleWaitList_[HCOMM_MAX_HANDLE_ID] = {0};
    HcommHandle curHandleId_ = HCOMM_INVALID_HANDLE_ID;
};
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H__
#endif
