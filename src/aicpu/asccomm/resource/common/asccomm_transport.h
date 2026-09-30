/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_TRANSPORT_H
#define ASCCOMM_TRANSPORT_H

#include "hcomm_res_entity_defs.h"
#include "asccomm_primitives.h"
#include "../../common/utils/asccomm_result.h"
#include "../connection/asccomm_backend_types.h"

AsccommResult asccomm_write(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
AsccommResult asccomm_write_reduce(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn);
AsccommResult asccomm_write_with_notify(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    uint32_t remoteNotifyIdx);
AsccommResult asccomm_write_reduce_with_notify(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn, uint32_t remoteNotifyIdx);
AsccommResult asccomm_read(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
AsccommResult asccomm_read_reduce(
    HcommThreadEntity& thread, HcommChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn);
AsccommResult asccomm_batch_transfer(
    HcommThreadEntity& thread, HcommChannelEntity& channel, const HcommBatchTransferDesc* transferDescs,
    uint32_t transferDescNum);
AsccommResult asccomm_notify_record(HcommThreadEntity& thread, HcommChannelEntity& channel, uint32_t remoteNotifyIdx);
AsccommResult asccomm_notify_wait(
    HcommThreadEntity& thread, const HcommChannelEntity& channel, uint32_t localNotifyIdx, uint32_t timeout);
AsccommResult asccomm_fence(HcommChannelEntity& channel);
AsccommResult asccomm_drain(HcommThreadEntity& thread, HcommChannelEntity& channel);

#endif // ASCCOMM_TRANSPORT_H
