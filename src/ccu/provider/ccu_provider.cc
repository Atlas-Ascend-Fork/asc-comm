/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_dataplane_abi.h"

#include <mutex>
#include <unordered_map>
#include <vector>

#include "ccu_provider_internal.h"

namespace {

struct OwnedChannelSnapshot {
    uint32_t dieId{0};
    uint32_t channelId{0};
    uint64_t remoteAddress{0};
    uint32_t remoteSize{0};
    uint32_t remoteTokenId{0};
    uint32_t remoteTokenValue{0};
    std::vector<AscCommCcuChannelEntry> entries;
};

std::mutex g_channelMutex;
std::unordered_map<AscCommCcuChannelHandle, OwnedChannelSnapshot> g_channels;

CcuResult UpsertChannel(AscCommCcuChannelHandle channel, const AscCommCcuChannelSnapshot *snapshot)
{
    if (channel == 0 || snapshot == nullptr) return CCU_E_PTR;
    if (snapshot->abiVersion != ASCCOMM_CCU_DATAPLANE_ABI_VERSION ||
        snapshot->structSize < sizeof(AscCommCcuChannelSnapshot)) return CCU_E_NOT_SUPPORT;
    if (snapshot->entryCount != 0 && snapshot->entries == nullptr) return CCU_E_PTR;

    OwnedChannelSnapshot owned{};
    owned.dieId = snapshot->dieId;
    owned.channelId = snapshot->channelId;
    owned.remoteAddress = snapshot->remoteAddress;
    owned.remoteSize = snapshot->remoteSize;
    owned.remoteTokenId = snapshot->remoteTokenId;
    owned.remoteTokenValue = snapshot->remoteTokenValue;
    const auto *entries = static_cast<const AscCommCcuChannelEntry *>(snapshot->entries);
    if (snapshot->entryCount != 0) {
        owned.entries.assign(entries, entries + snapshot->entryCount);
    }

    std::lock_guard<std::mutex> lock(g_channelMutex);
    g_channels[channel] = std::move(owned);
    return CCU_SUCCESS;
}

CcuResult RemoveChannel(AscCommCcuChannelHandle channel)
{
    if (channel == 0) return CCU_E_PARA;
    std::lock_guard<std::mutex> lock(g_channelMutex);
    g_channels.erase(channel);
    return CCU_SUCCESS;
}

} // namespace

namespace hcomm {

CcuResult CcuReadChannelSnapshot(AscCommCcuChannelHandle channel, uint32_t index, CcuChannelData &data)
{
    std::lock_guard<std::mutex> lock(g_channelMutex);
    const auto iter = g_channels.find(channel);
    if (iter == g_channels.end()) return CCU_E_NOT_FOUND;
    if (index >= iter->second.entries.size()) return CCU_E_PARA;

    const auto &snapshot = iter->second;
    const auto &entry = snapshot.entries[index];
    data.dieId = snapshot.dieId;
    data.channelId = snapshot.channelId;
    data.localCkeId = entry.localCkeId;
    data.localXnId = entry.localXnId;
    data.remoteCkeId = entry.remoteCkeId;
    data.remoteXnId = entry.remoteXnId;
    data.remoteAddress = snapshot.remoteAddress;
    data.remoteSize = snapshot.remoteSize;
    data.remoteTokenId = snapshot.remoteTokenId;
    data.remoteTokenValue = snapshot.remoteTokenValue;
    return CCU_SUCCESS;
}

} // namespace hcomm

CcuResult CcuDataPlaneQueryInstanceResourceRequest(uint32_t instanceType, AscCommCcuResourceRequest *request);
CcuResult CcuDataPlaneInstanceCreate(const AscCommCcuInstanceCreateParam *param, CcuInsHandle *insHandle);
CcuResult CcuDataPlaneInstanceDestroy(CcuInsHandle insHandle, uint32_t deviceLogicId);

extern "C" const AscCommCcuDataPlaneV1 *AscCommCcuGetDataPlaneV1(void)
{
    static const AscCommCcuDataPlaneV1 dataPlane{
        ASCCOMM_CCU_DATAPLANE_ABI_VERSION,
        sizeof(AscCommCcuDataPlaneV1),
        CcuDataPlaneQueryInstanceResourceRequest,
        CcuDataPlaneInstanceCreate,
        CcuDataPlaneInstanceDestroy,
        UpsertChannel,
        RemoveChannel,
    };
    return &dataPlane;
}
