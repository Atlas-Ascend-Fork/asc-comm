/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_channel_get_stub.h"

#include <algorithm>

namespace hcomm {
namespace {

HcommCcuChannelPod channelPodStub{};
std::vector<HcommCcuChannelResourcePod> resourceStub;
HcommResult firstQueryResultStub = HCCL_SUCCESS;
HcommResult secondQueryResultStub = HCCL_SUCCESS;
bool channelPodStubConfigured = false;

} // namespace

void SetHcommCcuChannelGetPodStub(const HcommCcuChannelPod &channelPod,
    const std::vector<HcommCcuChannelResourcePod> &resources)
{
    channelPodStub = channelPod;
    resourceStub = resources;
    firstQueryResultStub = HCCL_SUCCESS;
    secondQueryResultStub = HCCL_SUCCESS;
    channelPodStubConfigured = true;
}

void SetHcommCcuChannelGetPodStubResults(HcommResult firstQueryResult, HcommResult secondQueryResult)
{
    firstQueryResultStub = firstQueryResult;
    secondQueryResultStub = secondQueryResult;
}

void ResetHcommCcuChannelGetPodStub()
{
    channelPodStub = {};
    resourceStub.clear();
    firstQueryResultStub = HCCL_SUCCESS;
    secondQueryResultStub = HCCL_SUCCESS;
    channelPodStubConfigured = false;
}

} // namespace hcomm

HcommResult HcommCcuChannelGetPod(ChannelHandle, HcommCcuChannelPod *channelPod,
    HcommCcuChannelResourcePod *resources, uint32_t *resourceNum)
{
    if (channelPod == nullptr || resourceNum == nullptr || !hcomm::channelPodStubConfigured) {
        return HCCL_E_PTR;
    }

    const bool firstQuery = resources == nullptr;
    const HcommResult configuredResult = firstQuery ? hcomm::firstQueryResultStub : hcomm::secondQueryResultStub;
    if (configuredResult != HCCL_SUCCESS) {
        return configuredResult;
    }

    const uint32_t capacity = *resourceNum;
    *channelPod = hcomm::channelPodStub;
    *resourceNum = static_cast<uint32_t>(hcomm::resourceStub.size());
    if (firstQuery) {
        return HCCL_SUCCESS;
    }
    if (capacity < hcomm::resourceStub.size()) {
        return HCCL_E_PARA;
    }
    std::copy(hcomm::resourceStub.begin(), hcomm::resourceStub.end(), resources);
    return HCCL_SUCCESS;
}
