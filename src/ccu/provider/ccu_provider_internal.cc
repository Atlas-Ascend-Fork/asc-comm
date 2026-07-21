/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_provider_internal.h"

#include <mutex>

#include "ccu_instance.h"

namespace hcomm {
namespace {

    std::mutex g_hostServicesMutex;
    const AscCommCcuHostServicesV1 *g_hostServices = nullptr;
    uint32_t g_hostServicesRefCount = 0;
    thread_local CcuInstance *g_currentInstance = nullptr;

    const AscCommCcuHostServicesV1 *HostServices()
    {
        std::lock_guard<std::mutex> lock(g_hostServicesMutex);
        return g_hostServices;
    }

    HcclResult ToHccl(CcuResult result)
    {
        return static_cast<HcclResult>(result);
    }

} // namespace

CcuResult CcuRetainHostServices(const AscCommCcuHostServicesV1 *services)
{
    if (services == nullptr)
        return CCU_E_PTR;
    if (services->abiVersion != ASCCOMM_CCU_DATAPLANE_ABI_VERSION
        || services->structSize < sizeof(AscCommCcuHostServicesV1))
        return CCU_E_NOT_SUPPORT;
    if (services->allocInstruction == nullptr || services->releaseInstruction == nullptr
        || services->submitInstruction == nullptr || services->queryMemoryToken == nullptr
        || services->getLaunchContext == nullptr || services->reportTask == nullptr)
        return CCU_E_NOT_SUPPORT;

    std::lock_guard<std::mutex> lock(g_hostServicesMutex);
    if (g_hostServices != nullptr && g_hostServices != services)
        return CCU_E_UNAVAIL;
    g_hostServices = services;
    ++g_hostServicesRefCount;
    return CCU_SUCCESS;
}

void CcuReleaseHostServices()
{
    std::lock_guard<std::mutex> lock(g_hostServicesMutex);
    if (g_hostServicesRefCount != 0)
        --g_hostServicesRefCount;
}

void CcuSetCurrentInstance(CcuInstance *instance)
{
    g_currentInstance = instance;
}

CcuInstance *CcuGetCurrentInstance()
{
    return g_currentInstance;
}

CcuResult CcuGetCurrentDevice(uint32_t &deviceLogicId)
{
    if (g_currentInstance == nullptr)
        return CCU_E_NOT_INITIALIZED;
    deviceLogicId = static_cast<uint32_t>(g_currentInstance->GetDeviceLogicId());
    return CCU_SUCCESS;
}

CcuResult CcuGetDieEnableInfo(int32_t deviceLogicId, uint8_t dieId, bool &enabled)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != deviceLogicId) {
        return CCU_E_NOT_INITIALIZED;
    }
    if (dieId >= ASCCOMM_CCU_MAX_DIE_NUM)
        return CCU_E_PARA;
    enabled = g_currentInstance->GetDieEnabled(dieId);
    return CCU_SUCCESS;
}

CcuResult CcuQueryMemoryToken(uint64_t address, uint64_t size, uint64_t &tokenId, uint64_t &tokenValue)
{
    const auto *services = HostServices();
    if (services == nullptr)
        return CCU_E_NOT_INITIALIZED;
    return services->queryMemoryToken(services->userData, address, size, &tokenId, &tokenValue);
}

CcuResult CcuQueryDeviceMemoryToken(uint32_t deviceLogicId, uint64_t, uint64_t &tokenId, uint64_t &tokenValue)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != static_cast<int32_t>(deviceLogicId)) {
        return CCU_E_NOT_INITIALIZED;
    }
    const auto &token = g_currentInstance->GetDeviceMemoryToken();
    tokenId = token.tokenId;
    tokenValue = token.tokenValue;
    return CCU_SUCCESS;
}

CcuResult CcuQueryChannel(AscCommCcuChannelHandle channel, uint32_t index, CcuChannelData &data)
{
    return CcuReadChannelSnapshot(channel, index, data);
}

CcuResult CcuSubmitInstruction(
    uint32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId, const void *instructions, uint64_t size)
{
    const auto *services = HostServices();
    if (services == nullptr)
        return CCU_E_NOT_INITIALIZED;
    return services->submitInstruction(
        services->userData, deviceLogicId, dieId, startInstructionId, instructions, size);
}

const AscCommCcuHostServicesV1 *CcuGetHostServices()
{
    return HostServices();
}

HcclResult CcuDevMgrImp::GetCcuResourceSpaceTokenInfo(
    int32_t deviceLogicId, uint8_t dieId, uint64_t &tokenId, uint64_t &tokenValue)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != deviceLogicId
        || dieId >= ASCCOMM_CCU_MAX_DIE_NUM)
        return ToHccl(CCU_E_NOT_INITIALIZED);
    const auto &token = g_currentInstance->GetResourceSpaceToken(dieId);
    tokenId = token.tokenId;
    tokenValue = token.tokenValue;
    return HCCL_SUCCESS;
}

HcclResult CcuDevMgrImp::GetLoopChannelId(
    int32_t deviceLogicId, uint8_t sourceDieId, uint8_t destinationDieId, uint32_t &channelId)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != deviceLogicId) {
        return ToHccl(CCU_E_NOT_INITIALIZED);
    }
    if (sourceDieId >= ASCCOMM_CCU_MAX_DIE_NUM || destinationDieId >= ASCCOMM_CCU_MAX_DIE_NUM) {
        return ToHccl(CCU_E_PARA);
    }
    channelId = g_currentInstance->GetLoopChannelId(sourceDieId, destinationDieId);
    return HCCL_SUCCESS;
}

HcclResult CcuDevMgrImp::AllocIns(int32_t deviceLogicId, uint8_t dieId, uint32_t count, ResInfo &info)
{
    const auto *services = HostServices();
    if (services == nullptr)
        return ToHccl(CCU_E_NOT_INITIALIZED);
    AscCommCcuResourceSpan span{};
    CcuResult result = services->allocInstruction(services->userData, deviceLogicId, dieId, count, &span);
    info = {span.startId, span.count};
    return ToHccl(result);
}

HcclResult CcuDevMgrImp::ReleaseIns(int32_t deviceLogicId, uint8_t dieId, const ResInfo &info)
{
    const auto *services = HostServices();
    if (services == nullptr)
        return ToHccl(CCU_E_NOT_INITIALIZED);
    const AscCommCcuResourceSpan span{info.startId, info.num};
    return ToHccl(services->releaseInstruction(services->userData, deviceLogicId, dieId, &span));
}

HcclResult CcuDevMgrImp::GetMissionKey(int32_t deviceLogicId, uint8_t dieId, uint32_t &missionKey)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != deviceLogicId
        || dieId >= ASCCOMM_CCU_MAX_DIE_NUM)
        return ToHccl(CCU_E_NOT_INITIALIZED);
    missionKey = g_currentInstance->GetMissionKey(dieId);
    return HCCL_SUCCESS;
}

HcclResult CcuDevMgrImp::GetInstructionNum(int32_t, uint8_t, uint32_t &)
{
    return ToHccl(CCU_E_NOT_SUPPORT);
}

HcclResult CcuDevMgrImp::GetXnBaseAddr(uint32_t deviceLogicId, uint8_t dieId, uint64_t &address)
{
    if (g_currentInstance == nullptr || g_currentInstance->GetDeviceLogicId() != static_cast<int32_t>(deviceLogicId)
        || dieId >= ASCCOMM_CCU_MAX_DIE_NUM)
        return ToHccl(CCU_E_NOT_INITIALIZED);
    address = g_currentInstance->GetXnBaseAddress(dieId);
    return HCCL_SUCCESS;
}

} // namespace hcomm
