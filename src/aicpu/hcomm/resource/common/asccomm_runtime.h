/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RUNTIME_H
#define ASCCOMM_RUNTIME_H

// Internal POD RTSQ runtime. This header is not part of the public API.

#include "hcomm/hcomm_pod_resource.h"
#include "hccl/hccl_types.h"
#include "../connection/asccomm_backend_types.h"

namespace Asc {
struct UbJettyLiteId;
}

HcclResult asccomm_rtsq_launch(HcommRtsqContext &context);
HcclResult asccomm_rtsq_try_launch(HcommRtsqContext &context);
HcclResult asccomm_rtsq_notify_wait(HcommRtsqContext &context, uint32_t notifyId, uint32_t timeout);
HcclResult asccomm_rtsq_notify_record(HcommRtsqContext &context, uint32_t notifyId);
HcclResult asccomm_rtsq_sdma_copy(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size);
HcclResult asccomm_rtsq_sdma_reduce(HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr,
                                  uint32_t size, const Asc::ReduceIn &reduceIn);
HcclResult asccomm_rtsq_ub_db_send(
    HcommRtsqContext &context, const Asc::UbJettyLiteId &jettyId, uint16_t piValue);
HcclResult asccomm_rtsq_rdma_db_send(HcommRtsqContext &context, uint64_t dbAddr, uint64_t dbValue);

// RTSQ硬件属性(localDevId/sqBaseAddr/sqDepth/sqHead/sqTail)首用时在device侧解析：
// drvGetLocalDevIDByHostDevID为device-only驱动接口(见ascend_hal_base.h)，
// host控制面不查询硬件，仅填streamId/sqId/localSqeAddr，数据面在AICPU上自解析
HcclResult asccomm_ensure_rtsq_resolved(HcommThreadEntity &thread);

HcclResult asccomm_task_launch(HcommThreadEntity **threads, uint32_t threadNum);
HcclResult asccomm_try_launch(HcommThreadEntity **threads, uint32_t threadNum);

#endif // ASCCOMM_RUNTIME_H
