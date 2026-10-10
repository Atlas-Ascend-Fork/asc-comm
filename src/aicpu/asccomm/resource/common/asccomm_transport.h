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

#include "asccomm_res_defs.h"
#include "asccomm_primitives.h"
#include "../../common/utils/asccomm_result.h"
#include "../connection/asccomm_backend_types.h"

AsccommResult asccomm_write(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
AsccommResult asccomm_write_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn);
AsccommResult asccomm_write_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    uint32_t remoteNotifyIdx);
AsccommResult asccomm_write_reduce_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn, uint32_t remoteNotifyIdx);
AsccommResult asccomm_read(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
AsccommResult asccomm_read_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn);
AsccommResult asccomm_batch_transfer(
    ThreadEntity& thread, ChannelEntity& channel, const AsccommBatchTransferDesc* transferDescs,
    uint32_t transferDescNum);
AsccommResult asccomm_notify_record(ThreadEntity& thread, ChannelEntity& channel, uint32_t remoteNotifyIdx);
AsccommResult asccomm_notify_wait(
    ThreadEntity& thread, const ChannelEntity& channel, uint32_t localNotifyIdx, uint32_t timeout);
AsccommResult asccomm_fence(ChannelEntity& channel);
AsccommResult asccomm_drain(ThreadEntity& thread, ChannelEntity& channel);

#endif // ASCCOMM_TRANSPORT_H
