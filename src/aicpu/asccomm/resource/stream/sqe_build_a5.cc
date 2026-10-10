/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "sqe_build_a5.h"

namespace Asc {

void build_a5_sqe_rdma_db_send(uint32_t streamId, uint32_t taskId, uint64_t dbAddr, uint64_t dbValue, uint8_t* sqeIn)
{
    auto sqe = reinterpret_cast<Rt91095StarsWriteValueSqe*>(sqeIn);
    set_sqe_header_task_fields(sqe, taskId);
    sqe->header.type = static_cast<uint8_t>(Rt91095StarsSqeType::RT_91095_SQE_TYPE_WRITE_VALUE);
    sqe->kernelCredit = RT_STARS_DEFAULT_KERNEL_CREDIT;
    sqe->header.rtStreamId = streamId;
    sqe->header.taskId = taskId;
    sqe->writeAddrLow = dbAddr & MASK_32_BIT;
    sqe->writeAddrHigh = (dbAddr >> UINT32_BIT_NUM) & MASK_17_BIT;
    sqe->awsize = RtStarsWriteValueSizeType::RT_STARS_WRITE_VALUE_SIZE_TYPE_64BIT;
    sqe->writeValuePart[0] = static_cast<uint32_t>(dbValue & MASK_32_BIT);
    sqe->writeValuePart[1] = static_cast<uint32_t>((dbValue >> UINT32_BIT_NUM) & MASK_32_BIT);
    sqe->va = 1U;
}

} // namespace Asc
