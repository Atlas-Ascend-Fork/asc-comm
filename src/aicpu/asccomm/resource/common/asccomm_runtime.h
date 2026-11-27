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
#include "../connection/ub_completion.h"
#include "hcomm_res_entity_defs.h"
#include "asccomm_result.h"
#include "../connection/asccomm_backend_types.h"

namespace Asc {
struct UbJettyLiteId;
}

asccomm_result asccomm_rtsq_launch(ThreadEntity& thread);
asccomm_result asccomm_rtsq_try_launch(ThreadEntity& thread);
asccomm_result asccomm_rtsq_notify_wait(ThreadEntity& thread, uint32_t notifyId, uint32_t timeout);
asccomm_result asccomm_rtsq_notify_record(ThreadEntity& thread, uint32_t notifyId);
asccomm_result asccomm_rtsq_addr_notify_wait(ThreadEntity& thread, uint64_t waitAddr, uint64_t valueAddr, uint8_t last);
asccomm_result asccomm_rtsq_addr_notify_record(ThreadEntity& thread, uint64_t recordAddr, uint64_t valueAddr);
asccomm_result asccomm_rtsq_sdma_copy(ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size);
asccomm_result asccomm_rtsq_sdma_reduce(
    ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn);
asccomm_result asccomm_rtsq_ub_db_send(
    ThreadEntity& thread, const Asc::UbJettyLiteId& jettyId, uint16_t piValue, AsccommUbCompletionTag tag = {});
// Slow path only: submit a cumulative UB checkpoint and wait for its DB completion.
// No completion is inferred from PI or the software detour counter.
asccomm_result asccomm_rtsq_ub_checkpoint(
    ThreadEntity& thread, const Asc::UbJettyLiteId& jettyId, uint16_t pi, AsccommUbCompletionTag tag);
asccomm_result asccomm_rtsq_wait_ub(ThreadEntity& thread, AsccommUbCompletion& completion);
asccomm_result asccomm_rtsq_rdma_db_send(ThreadEntity& thread, uint64_t dbAddr, uint64_t dbValue);
asccomm_result asccomm_resolve_thread_rtsq(ThreadEntity* thread);
asccomm_result asccomm_validate_device_range(uint64_t addr, uint64_t len);

// RTSQ硬件属性由数据面在首次使用Thread resource时懒解析。
asccomm_result asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum);
asccomm_result asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum);

#endif // ASCCOMM_RUNTIME_H
