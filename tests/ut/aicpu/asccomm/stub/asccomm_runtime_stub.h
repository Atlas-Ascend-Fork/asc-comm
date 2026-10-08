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
#include <vector>

#include "asccomm_runtime.h"
#include "asccomm_transport.h"

struct AicpuRuntimeStubState {
    std::vector<ThreadEntity*> resolveCalls;
    struct CopyCall {
        HcommRtsqContext* context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
    };
    std::vector<CopyCall> copyCalls;
    struct ReduceCall {
        HcommRtsqContext* context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReduceCall> reduceCalls;
    struct NotifyRecordCall {
        HcommRtsqContext* context;
        uint32_t notifyId;
    };
    std::vector<NotifyRecordCall> notifyRecordCalls;
    struct NotifyWaitCall {
        HcommRtsqContext* context;
        uint32_t notifyId;
        uint32_t timeout;
    };
    std::vector<NotifyWaitCall> notifyWaitCalls;
    std::vector<ThreadHandle> launchThreads;
    std::vector<ThreadHandle> tryLaunchThreads;
    struct WriteCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
    };
    std::vector<WriteCall> writeCalls;
    struct WriteReduceCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<WriteReduceCall> writeReduceCalls;
    struct WriteNotifyCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        uint32_t notifyIdx;
    };
    std::vector<WriteNotifyCall> writeNotifyCalls;
    struct WriteReduceNotifyCall {
        ThreadEntity* thread;
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
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
    };
    std::vector<ReadCall> readCalls;
    struct ReadReduceCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReadReduceCall> readReduceCalls;
    struct BatchTransferCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        const AsccommBatchTransferDesc* descs;
        uint32_t descNum;
    };
    std::vector<BatchTransferCall> batchTransferCalls;
    struct ChannelNotifyRecordCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
        uint32_t notifyIdx;
    };
    std::vector<ChannelNotifyRecordCall> channelNotifyRecordCalls;
    struct ChannelNotifyWaitCall {
        ThreadEntity* thread;
        const ChannelEntity* channel;
        uint32_t notifyIdx;
        uint32_t timeout;
    };
    std::vector<ChannelNotifyWaitCall> channelNotifyWaitCalls;
    std::vector<ChannelEntity*> fenceCalls;
    struct DrainCall {
        ThreadEntity* thread;
        ChannelEntity* channel;
    };
    std::vector<DrainCall> drainCalls;

    AsccommResult resolveResult{ASCCOMM_SUCCESS};
    AsccommResult copyResult{ASCCOMM_SUCCESS};
    AsccommResult reduceResult{ASCCOMM_SUCCESS};
    AsccommResult notifyRecordResult{ASCCOMM_SUCCESS};
    AsccommResult notifyWaitResult{ASCCOMM_SUCCESS};
    AsccommResult launchResult{ASCCOMM_SUCCESS};
    AsccommResult tryLaunchResult{ASCCOMM_SUCCESS};
    AsccommResult writeResult{ASCCOMM_SUCCESS};
    AsccommResult writeReduceResult{ASCCOMM_SUCCESS};
    AsccommResult writeNotifyResult{ASCCOMM_SUCCESS};
    AsccommResult writeReduceNotifyResult{ASCCOMM_SUCCESS};
    AsccommResult readResult{ASCCOMM_SUCCESS};
    AsccommResult readReduceResult{ASCCOMM_SUCCESS};
    AsccommResult batchTransferResult{ASCCOMM_SUCCESS};
    AsccommResult channelNotifyRecordResult{ASCCOMM_SUCCESS};
    AsccommResult channelNotifyWaitResult{ASCCOMM_SUCCESS};
    AsccommResult fenceResult{ASCCOMM_SUCCESS};
    AsccommResult drainResult{ASCCOMM_SUCCESS};
};

extern AicpuRuntimeStubState g_runtimeStubState;

#endif // ASCCOMM_RUNTIME_STUB_H
