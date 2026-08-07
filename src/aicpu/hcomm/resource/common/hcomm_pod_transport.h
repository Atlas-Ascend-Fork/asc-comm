/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_TRANSPORT_H
#define HCOMM_POD_TRANSPORT_H

#include "hcomm_pod_resource.h"
#include "hcomm_pod_primitives.h"
#include "../connection/hcomm_pod_backend_types.h"

HcclResult HcommPodWrite(HcommThreadEntity &thread, HcommChannelEntity &channel,
                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
HcclResult HcommPodWriteReduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                               uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                               const Hccl::PodReduceIn &reduceIn);
HcclResult HcommPodWriteWithNotify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                   uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                   uint32_t remoteNotifyIdx);
HcclResult HcommPodWriteReduceWithNotify(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                         uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                                         const Hccl::PodReduceIn &reduceIn, uint32_t remoteNotifyIdx);
HcclResult HcommPodRead(HcommThreadEntity &thread, HcommChannelEntity &channel,
                        uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len);
HcclResult HcommPodReadReduce(HcommThreadEntity &thread, HcommChannelEntity &channel,
                              uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
                              const Hccl::PodReduceIn &reduceIn);
HcclResult HcommPodBatchTransfer(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                 const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum);
HcclResult HcommPodNotifyRecord(HcommThreadEntity &thread, HcommChannelEntity &channel,
                                uint32_t remoteNotifyIdx);
HcclResult HcommPodNotifyWait(HcommThreadEntity &thread, const HcommChannelEntity &channel,
                              uint32_t localNotifyIdx, uint32_t timeout);
HcclResult HcommPodFence(HcommChannelEntity &channel);
HcclResult HcommPodDrain(HcommThreadEntity &thread, HcommChannelEntity &channel);

#endif // HCOMM_POD_TRANSPORT_H
