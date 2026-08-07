/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_RUNTIME_H
#define HCOMM_POD_RUNTIME_H

// Internal POD RTSQ runtime. This header is not part of the public API.

#include "hcomm_pod_resource.h"
#include "hccl/hccl_types.h"
#include "../connection/hcomm_pod_backend_types.h"

namespace Hccl {
struct PodUbJettyLiteId;
}

HcclResult HcommPodRtsqLaunch(HcommRtsqContext &context);
HcclResult HcommPodRtsqTryLaunch(HcommRtsqContext &context);
HcclResult HcommPodRtsqNotifyWait(HcommRtsqContext &context, uint32_t notifyId, uint32_t timeout);
HcclResult HcommPodRtsqNotifyRecord(HcommRtsqContext &context, uint32_t notifyId);
HcclResult HcommPodRtsqSdmaCopy(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size);
HcclResult HcommPodRtsqSdmaReduce(HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr,
                                  uint32_t size, const Hccl::PodReduceIn &reduceIn);
HcclResult HcommPodRtsqUbDbSend(
    HcommRtsqContext &context, const Hccl::PodUbJettyLiteId &jettyId, uint16_t piValue);
HcclResult HcommPodRtsqRdmaDbSend(HcommRtsqContext &context, uint64_t dbAddr, uint64_t dbValue);

HcclResult HcommPodTaskLaunch(ThreadHandle *threads, uint32_t threadNum);
HcclResult HcommPodTryLaunch(ThreadHandle *threads, uint32_t threadNum);

#endif // HCOMM_POD_RUNTIME_H
