/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_SQE_BUILD_A5_H
#define HCOMM_POD_SQE_BUILD_A5_H

#include "hccl/base.h"
#include "../connection/ub_jetty_lite.h"
#include "sqe_v82.h"

namespace Hccl {

constexpr u32 LOW_BITS = 16;

inline void SetPodSqeHeaderTaskFields(void *sqe, u32 taskId)
{
    auto header = reinterpret_cast<Rt91095StarsSqeHeader *>(sqe);
    header->rtStreamId = static_cast<uint16_t>(taskId);
    header->taskId = static_cast<uint16_t>(taskId >> LOW_BITS);
}

inline void BuildA5PodSqeNotifyWait(u32 streamId, u32 taskId, u32 notifyId, u32 timeout, uint8_t *sqeIn)
{
    (void)streamId;
    auto sqe = reinterpret_cast<Rt91095StarsNotifySqe *>(sqeIn);
    SetPodSqeHeaderTaskFields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_NOTIFY_WAIT);
    sqe->header.wrCqe = 0U;
    sqe->kernelCredit = RT_STARS_NEVER_TIMEOUT_KERNEL_CREDIT;
    sqe->cntFlag = false;
    sqe->clrFlag = true;
    sqe->subType = static_cast<uint16_t>(Rt91095NotifySubType::NOTIFY_SUB_TYPE_SINGLE_NOTIFY_WAIT);
    sqe->notifyId = notifyId;
    sqe->timeout = timeout;
}

inline void BuildA5PodSqeNotifyRecord(u32 streamId, u32 taskId, u32 notifyId, uint8_t *sqeIn)
{
    (void)streamId;
    auto sqe = reinterpret_cast<Rt91095StarsNotifySqe *>(sqeIn);
    SetPodSqeHeaderTaskFields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_NOTIFY_RECORD);
    sqe->header.wrCqe = 0U;
    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->subType = static_cast<uint16_t>(Rt91095NotifySubType::NOTIFY_SUB_TYPE_SINGLE_NOTIFY_RECORD);
    sqe->notifyId = notifyId;
}

inline void BuildA5PodSqeSdmaCopy(u32 streamId, u32 taskId, u64 dstAddr, u64 srcAddr, u32 size,
                               u32 partId, u32 opcode, uint8_t *sqeIn)
{
    (void)streamId;
    auto sqe = reinterpret_cast<Rt91095StarsMemcpySqe *>(sqeIn);
    SetPodSqeHeaderTaskFields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_SDMA);
    sqe->header.wrCqe = 0U;
    sqe->opcode = opcode;
    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->sssv = 1U;
    sqe->dssv = 1U;
    sqe->sns = 1U;
    sqe->dns = 1U;
    sqe->mapamPartId = partId;
    sqe->u.strideMode0.lengthMove = size;
    sqe->u.strideMode0.srcAddrLow = static_cast<uint32_t>(srcAddr & 0xffffffffULL);
    sqe->u.strideMode0.srcAddrHigh = static_cast<uint32_t>(srcAddr >> 32U);
    sqe->u.strideMode0.dstAddrLow = static_cast<uint32_t>(dstAddr & 0xffffffffULL);
    sqe->u.strideMode0.dstAddrHigh = static_cast<uint32_t>(dstAddr >> 32U);
}

inline void BuildA5PodSqeUbDbSend(u32 streamId, u32 taskId, const PodUbJettyLiteId &jettyLiteId,
                               u16 piValue, uint8_t *sqeIn)
{
    (void)streamId;
    auto sqe = reinterpret_cast<Rt91095StarsUbdmaDBmodeSqe *>(sqeIn);
    SetPodSqeHeaderTaskFields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_UBDMA);
    sqe->mode = Rt91095UbDmaSqeMode::RT_91095_SQE_DOORBELL_MODE;
    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->doorbellNum = 1U;
    sqe->jettyId1 = jettyLiteId.GetJettyId();
    sqe->funcId1 = jettyLiteId.GetFuncId();
    sqe->piValue1 = piValue;
    sqe->dieId1 = jettyLiteId.GetDieId();
}

void BuildA5PodSqeRdmaDbSend(u32 streamId, u32 taskId, u64 dbAddr, u64 dbValue, uint8_t *sqeIn);

} // namespace Hccl

#endif // HCOMM_POD_SQE_BUILD_A5_H
