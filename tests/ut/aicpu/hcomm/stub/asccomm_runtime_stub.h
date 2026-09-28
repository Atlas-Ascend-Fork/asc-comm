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
    std::vector<HcommThreadEntity *> resolveCalls;
    struct CopyCall {
        HcommRtsqContext *context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
    };
    std::vector<CopyCall> copyCalls;
    struct ReduceCall {
        HcommRtsqContext *context;
        uint64_t dst;
        uint64_t src;
        uint32_t size;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReduceCall> reduceCalls;
    struct NotifyRecordCall {
        HcommRtsqContext *context;
        uint32_t notifyId;
    };
    std::vector<NotifyRecordCall> notifyRecordCalls;
    struct NotifyWaitCall {
        HcommRtsqContext *context;
        uint32_t notifyId;
        uint32_t timeout;
    };
    std::vector<NotifyWaitCall> notifyWaitCalls;
    std::vector<ThreadHandle> launchThreads;
    std::vector<ThreadHandle> tryLaunchThreads;
    struct WriteCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
    };
    std::vector<WriteCall> writeCalls;
    struct WriteReduceCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<WriteReduceCall> writeReduceCalls;
    struct WriteNotifyCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        uint32_t notifyIdx;
    };
    std::vector<WriteNotifyCall> writeNotifyCalls;
    struct WriteReduceNotifyCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t local;
        uintptr_t remote;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
        uint32_t notifyIdx;
    };
    std::vector<WriteReduceNotifyCall> writeReduceNotifyCalls;
    struct ReadCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
    };
    std::vector<ReadCall> readCalls;
    struct ReadReduceCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uintptr_t dst;
        uintptr_t src;
        uint64_t len;
        Asc::DataType dataType;
        Asc::ReduceOp reduceOp;
    };
    std::vector<ReadReduceCall> readReduceCalls;
    struct BatchTransferCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        const HcommBatchTransferDesc *descs;
        uint32_t descNum;
    };
    std::vector<BatchTransferCall> batchTransferCalls;
    struct ChannelNotifyRecordCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
        uint32_t notifyIdx;
    };
    std::vector<ChannelNotifyRecordCall> channelNotifyRecordCalls;
    struct ChannelNotifyWaitCall {
        HcommThreadEntity *thread;
        const HcommChannelEntity *channel;
        uint32_t notifyIdx;
        uint32_t timeout;
    };
    std::vector<ChannelNotifyWaitCall> channelNotifyWaitCalls;
    std::vector<HcommChannelEntity *> fenceCalls;
    struct DrainCall {
        HcommThreadEntity *thread;
        HcommChannelEntity *channel;
    };
    std::vector<DrainCall> drainCalls;

    HcclResult resolveResult{HCCL_SUCCESS};
    HcclResult copyResult{HCCL_SUCCESS};
    HcclResult reduceResult{HCCL_SUCCESS};
    HcclResult notifyRecordResult{HCCL_SUCCESS};
    HcclResult notifyWaitResult{HCCL_SUCCESS};
    HcclResult launchResult{HCCL_SUCCESS};
    HcclResult tryLaunchResult{HCCL_SUCCESS};
    HcclResult writeResult{HCCL_SUCCESS};
    HcclResult writeReduceResult{HCCL_SUCCESS};
    HcclResult writeNotifyResult{HCCL_SUCCESS};
    HcclResult writeReduceNotifyResult{HCCL_SUCCESS};
    HcclResult readResult{HCCL_SUCCESS};
    HcclResult readReduceResult{HCCL_SUCCESS};
    HcclResult batchTransferResult{HCCL_SUCCESS};
    HcclResult channelNotifyRecordResult{HCCL_SUCCESS};
    HcclResult channelNotifyWaitResult{HCCL_SUCCESS};
    HcclResult fenceResult{HCCL_SUCCESS};
    HcclResult drainResult{HCCL_SUCCESS};
};

extern AicpuRuntimeStubState g_runtimeStubState;

#endif // ASCCOMM_RUNTIME_STUB_H
