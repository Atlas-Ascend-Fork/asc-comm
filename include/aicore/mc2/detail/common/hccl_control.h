/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hccl_control.h
 * \brief
 */
#ifndef IMPL_HCCL_HCCL_CONTROL_H
#define IMPL_HCCL_HCCL_CONTROL_H

#include "hccl_inner_def.h"

namespace AscendC {

constexpr uint8_t SINGLE_COMM_NUM = 1;
constexpr uint8_t MULTI_COMM_NUM = 2;

#define HCCL_CHECK_RESTART(msgArea, expression)  \
    do {                                         \
        if (unlikely(CheckIfRestart(msgArea))) { \
            expression;                          \
        }                                        \
    } while (0)

#define HCCL_CHECK_STATUS(status, expression) \
    do {                                      \
        if (status) {                         \
            expression;                       \
        }                                     \
    } while (0)

__aicore__ inline void GetRestartFromContext(GM_ADDR context, uint8_t& restart)
{
    HCCL_CHECK_STATUS(context == nullptr, return );
    __gm__ HcclCombineOpParam* hcclContext = (__gm__ HcclCombineOpParam*)context;
    uint64_t msgAddr = hcclContext->workSpace;
    HCCL_CHECK_STATUS(msgAddr == 0, return );
    __gm__ HcclMsgArea* hcclMsgArea = (__gm__ HcclMsgArea*)msgAddr;
    __gm__ ControlHcclMsg* controlMsgGM = &hcclMsgArea->controlMsg;
    FlushDataCache(controlMsgGM);
    restart += controlMsgGM->restart;
}

__aicore__ inline void ResetRestartFlag(GM_ADDR context)
{
    HCCL_CHECK_STATUS(context == nullptr, return );
    __gm__ HcclCombineOpParam* hcclContext = (__gm__ HcclCombineOpParam*)context;
    uint64_t msgAddr = hcclContext->workSpace;
    HCCL_CHECK_STATUS(msgAddr == 0, return );
    __gm__ HcclMsgArea* hcclMsgArea = (__gm__ HcclMsgArea*)msgAddr;
    __gm__ ControlHcclMsg* controlMsgGM = &hcclMsgArea->controlMsg;
    controlMsgGM->restart = 0;
    controlMsgGM->restarting = 1;
    controlMsgGM->resetSeq = 1;
    FlushDataCache(controlMsgGM);
}

__aicore__ inline uint8_t GetRestart(uint8_t ctxNum)
{
    uint8_t restart = 0;
    // Maximum support for dual communication domains
    if (ctxNum >= SINGLE_COMM_NUM) {
        GetRestartFromContext(AscendC::GetHcclContext<0>(), restart);
    }
    if (ctxNum >= MULTI_COMM_NUM) {
        GetRestartFromContext(AscendC::GetHcclContext<1>(), restart);
    }
    return restart;
}

__aicore__ inline void SetRestart(uint8_t ctxNum)
{
    if (GetBlockIdx() == 0) {
        // Maximum support for dual communication domains
        if (ctxNum >= SINGLE_COMM_NUM) {
            ResetRestartFlag(AscendC::GetHcclContext<0>());
        }
        if (ctxNum >= MULTI_COMM_NUM) {
            ResetRestartFlag(AscendC::GetHcclContext<1>());
        }
    }
}

__aicore__ inline bool CheckIfRestart(__gm__ HcclMsgArea* msgArea)
{
#if AICORE_EXCEPTION_RESTART == 1
    __gm__ ControlHcclMsg* controlMsgGM = &msgArea->controlMsg;
    FlushDataCache(controlMsgGM);
    if (controlMsgGM->restart > 0) {
        return true;
    }
#endif
    return false;
}
} // namespace AscendC

#endif // IMPL_HCCL_HCCL_CONTROL_H
