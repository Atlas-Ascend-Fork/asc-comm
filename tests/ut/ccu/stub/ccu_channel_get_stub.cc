/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_channel_get_stub.h"

#include "hcomm/common/ccu_device_context.h"

namespace asc {
namespace {

HcommCcuChannelPod channelPodStub{};
int32_t queryResultStub = HCCL_SUCCESS;
bool channelPodStubConfigured = false;

int32_t HcommCcuChannelQueryStubFn(ChannelHandle, HcommCcuChannelPod* channelPod)
{
    if (channelPod == nullptr || !channelPodStubConfigured) {
        return HCCL_E_PTR;
    }
    if (queryResultStub != HCCL_SUCCESS) {
        return queryResultStub;
    }

    *channelPod = channelPodStub;
    return HCCL_SUCCESS;
}

} // namespace

void SetHcommCcuChannelQueryStub(const HcommCcuChannelPod& channelPod)
{
    channelPodStub = channelPod;
    queryResultStub = HCCL_SUCCESS;
    channelPodStubConfigured = true;

    HcommCcuControlOpsPod controlOps{};
    controlOps.header.version = HCOMM_CCU_CONTROL_ABI_VERSION;
    controlOps.header.magicWord = HCOMM_CCU_CONTROL_OPS_MAGIC_WORD;
    controlOps.header.size = sizeof(HcommCcuControlOpsPod);
    controlOps.channelQuery = &HcommCcuChannelQueryStubFn;
    asc::set_current_ccu_control_ops(controlOps);
}

void SetHcommCcuChannelQueryStubResult(int32_t result) { queryResultStub = result; }

void ResetHcommCcuChannelQueryStub()
{
    channelPodStub = {};
    queryResultStub = HCCL_SUCCESS;
    channelPodStubConfigured = false;
    asc::set_current_ccu_control_ops({});
}

} // namespace asc
