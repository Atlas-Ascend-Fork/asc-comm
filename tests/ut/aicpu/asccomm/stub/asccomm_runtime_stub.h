/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RUNTIME_STUB_H
#define ASCCOMM_RUNTIME_STUB_H

#include <cstdint>
#include <limits>
#include <vector>

#include "asccomm_runtime.h"
#include "asccomm_transport.h"

struct AicpuRuntimeStubState {
    std::vector<HcommThreadEntity*> resolveCalls;
    struct CopyCall {
        HcommThreadEntity* context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
    };
    std::vector<CopyCall> copyCalls;
    struct ReduceCall {
        HcommThreadEntity* context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReduceCall> reduceCalls;
    struct NotifyRecordCall {
        HcommThreadEntity* context;
        uint32_t notifyId;
    };
    std::vector<NotifyRecordCall> notifyRecordCalls;
    struct AddrNotifyCall {
        HcommThreadEntity* context;
        uint64_t waitAddr;
        uint64_t valueAddr;
        uint8_t last;
    };
    std::vector<AddrNotifyCall> addrNotifyWaitCalls;
    std::vector<AddrNotifyCall> addrNotifyRecordCalls;
    struct NotifyWaitCall {
        HcommThreadEntity* context;
        uint32_t notifyId;
        uint32_t timeout;
    };
    std::vector<NotifyWaitCall> notifyWaitCalls;
    std::vector<ThreadHandle> launchThreads;
    std::vector<ThreadHandle> tryLaunchThreads;
    struct WriteCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
    };
    std::vector<WriteCall> writeCalls;
    struct WriteReduceCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<WriteReduceCall> writeReduceCalls;
    struct WriteNotifyCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        uint32_t notifyIdx;
    };
    std::vector<WriteNotifyCall> writeNotifyCalls;
    struct WriteReduceNotifyCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
        uint32_t notifyIdx;
    };
    std::vector<WriteReduceNotifyCall> writeReduceNotifyCalls;
    struct ReadCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
    };
    std::vector<ReadCall> readCalls;
    struct ReadReduceCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReadReduceCall> readReduceCalls;
    struct SendCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uint64_t len;
    };
    std::vector<SendCall> sendCalls;
    struct SendWithImmCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uint64_t len;
        uint64_t immData;
    };
    std::vector<SendWithImmCall> sendWithImmCalls;
    struct BatchTransferCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        const asccomm_batch_transfer_desc* descs;
        uint32_t descNum;
    };
    std::vector<BatchTransferCall> batchTransferCalls;
    struct ChannelNotifyRecordCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
        uint32_t notifyIdx;
    };
    std::vector<ChannelNotifyRecordCall> channelNotifyRecordCalls;
    struct ChannelNotifyWaitCall {
        HcommThreadEntity* thread;
        const ChannelEntity* channel;
        uint32_t notifyIdx;
        uint32_t timeout;
    };
    std::vector<ChannelNotifyWaitCall> channelNotifyWaitCalls;
    std::vector<ChannelEntity*> fenceCalls;
    struct DrainCall {
        HcommThreadEntity* thread;
        ChannelEntity* channel;
    };
    std::vector<DrainCall> drainCalls;
    struct DeviceRangeQuery {
        uint64_t addr;
        uint64_t len;
    };
    std::vector<DeviceRangeQuery> deviceRangeQueries;
    struct DeviceRange {
        uint64_t base;
        uint64_t size;
    };
    DeviceRange deviceRange{0, std::numeric_limits<uint64_t>::max()};
    int32_t deviceRangeResult{0};

    asccomm_result resolveResult{ASCCOMM_SUCCESS};
    asccomm_result copyResult{ASCCOMM_SUCCESS};
    asccomm_result reduceResult{ASCCOMM_SUCCESS};
    asccomm_result notifyRecordResult{ASCCOMM_SUCCESS};
    asccomm_result notifyWaitResult{ASCCOMM_SUCCESS};
    asccomm_result launchResult{ASCCOMM_SUCCESS};
    asccomm_result tryLaunchResult{ASCCOMM_SUCCESS};
    asccomm_result writeResult{ASCCOMM_SUCCESS};
    asccomm_result writeReduceResult{ASCCOMM_SUCCESS};
    asccomm_result writeNotifyResult{ASCCOMM_SUCCESS};
    asccomm_result writeReduceNotifyResult{ASCCOMM_SUCCESS};
    asccomm_result readResult{ASCCOMM_SUCCESS};
    asccomm_result readReduceResult{ASCCOMM_SUCCESS};
    asccomm_result sendResult{ASCCOMM_SUCCESS};
    asccomm_result sendWithImmResult{ASCCOMM_SUCCESS};
    asccomm_result batchTransferResult{ASCCOMM_SUCCESS};
    asccomm_result channelNotifyRecordResult{ASCCOMM_SUCCESS};
    asccomm_result channelNotifyWaitResult{ASCCOMM_SUCCESS};
    asccomm_result fenceResult{ASCCOMM_SUCCESS};
    asccomm_result drainResult{ASCCOMM_SUCCESS};
};

extern AicpuRuntimeStubState g_runtimeStubState;

#endif // ASCCOMM_RUNTIME_STUB_H
