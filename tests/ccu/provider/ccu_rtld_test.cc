/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <dlfcn.h>

#include <cstdio>
#include <vector>

#include "ccu_dataplane_abi.h"

namespace {

using RegisterStart = CcuResult (*)(CcuInsHandle);
using RegisterKernel
    = CcuResult (*)(CcuInsHandle, uint32_t, const char *, const void *, const void **, uint32_t, CcuKernelHandle *);
using RegisterEnd = CcuResult (*)(CcuInsHandle);
using GetMemToken = CcuResult (*)(uint64_t, uint64_t, uint64_t *);

uint32_t g_instructionAllocCount = 0;
uint32_t g_instructionReleaseCount = 0;
uint32_t g_instructionSubmitCount = 0;

CcuResult EmptyKernel()
{
    return CCU_SUCCESS;
}

CcuResult AllocInstruction(void *, uint32_t, uint32_t, uint32_t count, AscCommCcuResourceSpan *span)
{
    if (span == nullptr)
        return CCU_E_PTR;
    *span = {0, count};
    ++g_instructionAllocCount;
    return CCU_SUCCESS;
}

CcuResult ReleaseInstruction(void *, uint32_t, uint32_t, const AscCommCcuResourceSpan *)
{
    ++g_instructionReleaseCount;
    return CCU_SUCCESS;
}

CcuResult SubmitInstruction(void *, uint32_t, uint32_t, uint32_t, const void *, uint64_t)
{
    ++g_instructionSubmitCount;
    return CCU_SUCCESS;
}

CcuResult QueryMemoryToken(void *, uint64_t, uint64_t, uint64_t *tokenId, uint64_t *tokenValue)
{
    if (tokenId == nullptr || tokenValue == nullptr)
        return CCU_E_PTR;
    *tokenId = 1;
    *tokenValue = 2;
    return CCU_SUCCESS;
}

CcuResult GetLaunchContext(void *, AscCommCcuThreadHandle, AscCommCcuLaunchContext *)
{
    return CCU_SUCCESS;
}

CcuResult ReportTask(void *, AscCommCcuThreadHandle, uint32_t, uint32_t, CcuKernelHandle, const AscCommCcuTask *)
{
    return CCU_SUCCESS;
}

bool CheckInstanceSnapshot(const AscCommCcuDataPlaneV1 &dataPlane, const AscCommCcuResourceRequest &request,
    RegisterStart registerStart, RegisterKernel registerKernel, RegisterEnd registerEnd, GetMemToken getMemToken)
{
    const AscCommCcuHostServicesV1 services{ASCCOMM_CCU_DATAPLANE_ABI_VERSION, sizeof(AscCommCcuHostServicesV1),
        nullptr, AllocInstruction, ReleaseInstruction, SubmitInstruction, QueryMemoryToken, GetLaunchContext,
        ReportTask};
    std::vector<AscCommCcuResourceEntry> entries;
    for (uint32_t type = 0; type < ASCCOMM_CCU_RES_TYPE_COUNT; ++type) {
        const uint32_t baseStart = type == ASCCOMM_CCU_RES_MISSION ? 0 : 1000U + type * 100U;
        entries.push_back({type, 0, baseStart, 1});
        const uint32_t count = request.count[type][0];
        if (count == 0)
            continue;
        const uint32_t first = count / 2;
        if (first != 0)
            entries.push_back({type, 0, 1100U + type * 100U, first});
        entries.push_back({type, 0, 1100U + type * 100U + first, count - first});
    }

    AscCommCcuInstanceCreateParam param{};
    param.abiVersion = ASCCOMM_CCU_DATAPLANE_ABI_VERSION;
    param.structSize = sizeof(param);
    param.instanceType = CCU_DEFAULT;
    param.dieEnabled[0] = 1;
    param.missionRequestType = request.missionRequestType;
    param.resourceEntryCount = entries.size();
    param.resourceEntries = entries.data();
    param.hostServices = &services;
    CcuInsHandle instance = 0;
    const CcuResult createResult = dataPlane.instanceCreate(&param, &instance);
    uint64_t token = 0;
    CcuKernelHandle kernel = 0;
    const bool operationsSucceeded
        = createResult == CCU_SUCCESS && registerStart(instance) == CCU_SUCCESS
          && registerKernel(instance, 0, "empty", reinterpret_cast<const void *>(EmptyKernel), nullptr, 0, &kernel)
                 == CCU_SUCCESS
          && registerEnd(instance) == CCU_SUCCESS && registerStart(instance) == CCU_SUCCESS
          && registerEnd(instance) == CCU_SUCCESS && getMemToken(1, 1, &token) == CCU_SUCCESS && token != 0;
    const CcuResult destroyResult = createResult == CCU_SUCCESS ? dataPlane.instanceDestroy(instance, 0) : createResult;
    const bool dynamicServicesUsed
        = g_instructionAllocCount != 0 && g_instructionReleaseCount != 0 && g_instructionSubmitCount != 0;
    if (createResult != CCU_SUCCESS || instance == 0 || kernel == 0 || !operationsSucceeded || !dynamicServicesUsed
        || destroyResult != CCU_SUCCESS) {
        std::fprintf(stderr, "snapshot create=%u handle=%llu operations=%d destroy=%u entries=%zu\n",
            static_cast<uint32_t>(createResult), static_cast<unsigned long long>(instance), operationsSucceeded,
            static_cast<uint32_t>(destroyResult), entries.size());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
        return 1;
    const bool foundDataPlane = dlsym(handle, "AscCommCcuGetDataPlaneV1") != nullptr;
    const bool foundControlPlane = dlsym(handle, "HcommCcuGetControlPlaneV1") != nullptr;
    const bool foundRegister = dlsym(handle, "HcommCcuKernelRegister") != nullptr;
    const bool foundLaunch = dlsym(handle, "HcommCcuKernelLaunch") != nullptr;
    const bool foundToken = dlsym(handle, "HcommCcuGetMemToken") != nullptr;
    const bool foundPrimitive = dlsym(handle, "CcuVariableAlloc") != nullptr;
    const bool foundProvider = dlsym(handle, "AscCommCcuRegisterProvider") != nullptr;
    const bool foundMirror = dlsym(handle, "AscCommCcuKernelLaunch") != nullptr;
    auto registerStart = reinterpret_cast<RegisterStart>(dlsym(handle, "HcommCcuKernelRegisterStart"));
    auto registerKernel = reinterpret_cast<RegisterKernel>(dlsym(handle, "HcommCcuKernelRegister"));
    auto registerEnd = reinterpret_cast<RegisterEnd>(dlsym(handle, "HcommCcuKernelRegisterEnd"));
    auto getMemToken = reinterpret_cast<GetMemToken>(dlsym(handle, "HcommCcuGetMemToken"));
    using Getter = const AscCommCcuDataPlaneV1 *(*)();
    auto getter = reinterpret_cast<Getter>(dlsym(handle, "AscCommCcuGetDataPlaneV1"));
    const auto *dataPlane = getter == nullptr ? nullptr : getter();
    AscCommCcuResourceRequest request{};
    const bool queried = dataPlane != nullptr && dataPlane->queryInstanceResourceRequest != nullptr
                         && dataPlane->queryInstanceResourceRequest(CCU_DEFAULT, &request) == CCU_SUCCESS;
    bool requestHasResource = false;
    for (uint32_t type = 0; type < ASCCOMM_CCU_RES_TYPE_COUNT; ++type) {
        requestHasResource = requestHasResource || request.count[type][0] != 0;
    }
    const bool validDataPlane
        = queried && dataPlane->abiVersion == ASCCOMM_CCU_DATAPLANE_ABI_VERSION
          && dataPlane->structSize >= sizeof(AscCommCcuDataPlaneV1) && dataPlane->instanceCreate != nullptr
          && dataPlane->instanceDestroy != nullptr && registerStart != nullptr && registerKernel != nullptr
          && registerEnd != nullptr && getMemToken != nullptr && requestHasResource
          && CheckInstanceSnapshot(*dataPlane, request, registerStart, registerKernel, registerEnd, getMemToken);
    if (!(foundDataPlane && foundRegister && foundLaunch && foundToken && foundPrimitive && validDataPlane
            && !foundControlPlane && !foundProvider && !foundMirror)) {
        std::fprintf(stderr,
            "symbols dp=%d reg=%d launch=%d token=%d prim=%d control=%d provider=%d "
            "mirror=%d valid=%d mission=%u\n",
            foundDataPlane, foundRegister, foundLaunch, foundToken, foundPrimitive, foundControlPlane, foundProvider,
            foundMirror, validDataPlane, request.count[ASCCOMM_CCU_RES_TYPE_COUNT - 1][0]);
    }
    (void)dlclose(handle);
    return foundDataPlane && foundRegister && foundLaunch && foundToken && foundPrimitive && validDataPlane
                   && !foundControlPlane && !foundProvider && !foundMirror
               ? 0
               : 1;
}
