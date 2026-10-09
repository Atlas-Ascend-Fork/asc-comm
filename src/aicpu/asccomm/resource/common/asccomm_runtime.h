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

// Internal resource RTSQ runtime. This header is not part of the public API.

#include "asccomm_res_defs.h"
#include "../../common/utils/asccomm_result.h"
#include "../connection/asccomm_backend_types.h"

namespace Asc {
struct UbJettyLiteId;
}

AsccommResult asccomm_rtsq_launch(HcommRtsqContext& context);
AsccommResult asccomm_rtsq_try_launch(HcommRtsqContext& context);
AsccommResult asccomm_rtsq_notify_wait(HcommRtsqContext& context, uint32_t notifyId, uint32_t timeout);
AsccommResult asccomm_rtsq_notify_record(HcommRtsqContext& context, uint32_t notifyId);
AsccommResult asccomm_rtsq_sdma_copy(HcommRtsqContext& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size);
AsccommResult asccomm_rtsq_sdma_reduce(
    HcommRtsqContext& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn);
AsccommResult asccomm_rtsq_ub_db_send(HcommRtsqContext& context, const Asc::UbJettyLiteId& jettyId, uint16_t piValue);
AsccommResult asccomm_rtsq_rdma_db_send(HcommRtsqContext& context, uint64_t dbAddr, uint64_t dbValue);
AsccommResult asccomm_resolve_thread_rtsq(ThreadEntity* thread);

// RTSQ硬件属性由数据面在首次使用Thread resource时懒解析。
AsccommResult asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum);
AsccommResult asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum);

#endif // ASCCOMM_RUNTIME_H
