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
 * \file hcomm_base.h
 * \brief Hcomm base class
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/common/hcomm_base.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_BASE_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_BASE_H

#include "hcomm_inner_def.h"

namespace AscendC {
template <CommEngine commEngine, CommProtocol commProtocol>
class HcommImpl {
public:
    __aicore__ inline HcommImpl(){};
    __aicore__ inline ~HcommImpl(){};
    template <bool commit = true, pipe_t commitPipe = PIPE_MTE3, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline HcommHandle Write(ChannelHandle channelHandle, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return -1;
    }

    template <bool commit = true, pipe_t commitPipe = PIPE_MTE3, pipe_t reqPipe = PIPE_MTE3>
    __aicore__ inline HcommHandle Read(ChannelHandle channelHandle, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return -1;
    }

    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Commit(HcommHandle handleId)
    {
        return -1;
    }

    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t Wait(HcommHandle handleId)
    {
        return -1;
    }
};
} // namespace AscendC

#endif // IMPL_HCOMM_BASE_H
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_BASE_H__
#endif
