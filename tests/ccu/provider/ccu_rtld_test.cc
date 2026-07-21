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

#include "ccu_types.h"
#include "ccu_dataplane_abi.h"
#include "ccu_res_repo.h"

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

} // namespace

extern "C" uint32_t HcommCcuAllocResHandleByInsType(int32_t, uint32_t, void *resourceHandleData, uint32_t size)
{
    if (resourceHandleData == nullptr)
        return CCU_E_PTR;
    if (size != sizeof(void *))
        return CCU_E_PARA;
    *static_cast<void **>(resourceHandleData) = reinterpret_cast<void *>(1);
    return CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuAllocResHandle(
    int32_t, const void *, uint32_t requestSize, void *resourceHandleData, uint32_t resourceHandleSize)
{
    if (resourceHandleData == nullptr)
        return CCU_E_PTR;
    if (requestSize != sizeof(hcomm::CcuResReq) || resourceHandleSize != sizeof(void *))
        return CCU_E_PARA;
    *static_cast<void **>(resourceHandleData) = reinterpret_cast<void *>(2);
    return CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuQueryResource(
    int32_t, void *resourceHandle, void *repositoryData, uint32_t repositorySize)
{
    if (resourceHandle == nullptr || repositoryData == nullptr)
        return CCU_E_PTR;
    if (repositorySize != sizeof(hcomm::CcuResRepository))
        return CCU_E_PARA;
    auto &repository = *static_cast<hcomm::CcuResRepository *>(repositoryData);
    repository = {};
    for (uint32_t die = 0; die < hcomm::CCU_MAX_IODIE_NUM; ++die) {
        for (uint32_t index = 0; index < 256; ++index) {
            repository.loopEngine[die].emplace_back(index, 1);
            repository.blockLoopEngine[die].emplace_back(index, 1);
            repository.ms[die].emplace_back(index, 1);
            repository.blockMs[die].emplace_back(index, 1);
            repository.cke[die].emplace_back(index, 1);
            repository.blockCke[die].emplace_back(index, 1);
            repository.continuousXn[die].emplace_back(index, 1);
            repository.xn[die].emplace_back(index, 1);
            repository.gsa[die].emplace_back(index, 1);
            repository.mission.mission[die].emplace_back(index, 1);
        }
    }
    return CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuReleaseResource(int32_t, void *resourceHandle)
{
    return resourceHandle == nullptr ? CCU_E_PTR : CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuAllocInstruction(
    int32_t, uint32_t, uint32_t count, void *instructionInfo, uint32_t instructionInfoSize)
{
    if (instructionInfo == nullptr)
        return CCU_E_PTR;
    if (instructionInfoSize != sizeof(hcomm::ResInfo))
        return CCU_E_PARA;
    *static_cast<hcomm::ResInfo *>(instructionInfo) = {0, count};
    ++g_instructionAllocCount;
    return CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuReleaseInstruction(int32_t, uint32_t, const void *, uint32_t instructionInfoSize)
{
    if (instructionInfoSize != sizeof(hcomm::ResInfo))
        return CCU_E_PARA;
    ++g_instructionReleaseCount;
    return CCU_SUCCESS;
}

extern "C" uint32_t HcommCcuSubmitInstruction(uint32_t, uint32_t, uint32_t, const void *, uint64_t)
{
    ++g_instructionSubmitCount;
    return CCU_SUCCESS;
}

namespace {

AscCommCcuResult QueryMemoryToken(void *, uint64_t, uint64_t, uint64_t *tokenId, uint64_t *tokenValue)
{
    if (tokenId == nullptr || tokenValue == nullptr)
        return CCU_E_PTR;
    *tokenId = 1;
    *tokenValue = 2;
    return CCU_SUCCESS;
}

AscCommCcuResult GetLaunchContext(void *, AscCommCcuThreadHandle, AscCommCcuLaunchContext *)
{
    return CCU_SUCCESS;
}

AscCommCcuResult ReportTask(
    void *, AscCommCcuThreadHandle, uint32_t, uint32_t, AscCommCcuKernelHandle, const AscCommCcuTask *)
{
    return CCU_SUCCESS;
}

bool CheckRegisterResourceBridge(const AscCommCcuDataPlaneV1 &dataPlane,
    RegisterStart registerStart, RegisterKernel registerKernel, RegisterEnd registerEnd, GetMemToken getMemToken)
{
    const AscCommCcuHostServicesV1 services{ASCCOMM_CCU_DATAPLANE_ABI_VERSION, sizeof(AscCommCcuHostServicesV1),
        nullptr, QueryMemoryToken, GetLaunchContext, ReportTask};

    AscCommCcuInstanceCreateParam param{};
    param.abiVersion = ASCCOMM_CCU_DATAPLANE_ABI_VERSION;
    param.structSize = sizeof(param);
    param.instanceType = CCU_DEFAULT;
    param.dieEnabled[0] = 1;
    param.hostServices = &services;
    CcuInsHandle instance = 0;
    const CcuResult createResult = static_cast<CcuResult>(dataPlane.instanceCreate(&param, &instance));
    uint64_t token = 0;
    CcuKernelHandle kernel = 0;
    const bool operationsSucceeded
        = createResult == CCU_SUCCESS && registerStart(instance) == CCU_SUCCESS
          && registerKernel(instance, 0, "empty", reinterpret_cast<const void *>(EmptyKernel), nullptr, 0, &kernel)
                 == CCU_SUCCESS
          && registerEnd(instance) == CCU_SUCCESS && registerStart(instance) == CCU_SUCCESS
          && registerEnd(instance) == CCU_SUCCESS && getMemToken(1, 1, &token) == CCU_SUCCESS && token != 0;
    const CcuResult destroyResult = createResult == CCU_SUCCESS
                                        ? static_cast<CcuResult>(dataPlane.instanceDestroy(instance, 0))
                                        : createResult;
    const bool dynamicServicesUsed
        = g_instructionAllocCount != 0 && g_instructionReleaseCount != 0 && g_instructionSubmitCount != 0;
    if (createResult != CCU_SUCCESS || instance == 0 || kernel == 0 || !operationsSucceeded || !dynamicServicesUsed
        || destroyResult != CCU_SUCCESS) {
        std::fprintf(stderr, "resource bridge create=%u handle=%llu operations=%d destroy=%u\n",
            static_cast<uint32_t>(createResult), static_cast<unsigned long long>(instance), operationsSucceeded,
            static_cast<uint32_t>(destroyResult));
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
    const bool validDataPlane
        = dataPlane != nullptr && dataPlane->abiVersion == ASCCOMM_CCU_DATAPLANE_ABI_VERSION
          && dataPlane->structSize >= sizeof(AscCommCcuDataPlaneV1) && dataPlane->instanceCreate != nullptr
          && dataPlane->instanceDestroy != nullptr && registerStart != nullptr && registerKernel != nullptr
          && registerEnd != nullptr && getMemToken != nullptr
          && CheckRegisterResourceBridge(*dataPlane, registerStart, registerKernel, registerEnd, getMemToken);
    if (!(foundDataPlane && foundRegister && foundLaunch && foundToken && foundPrimitive && validDataPlane
            && !foundControlPlane && !foundProvider && !foundMirror)) {
        std::fprintf(stderr,
            "symbols dp=%d reg=%d launch=%d token=%d prim=%d control=%d provider=%d "
            "mirror=%d valid=%d\n",
            foundDataPlane, foundRegister, foundLaunch, foundToken, foundPrimitive, foundControlPlane, foundProvider,
            foundMirror, validDataPlane);
    }
    (void)dlclose(handle);
    return foundDataPlane && foundRegister && foundLaunch && foundToken && foundPrimitive && validDataPlane
                   && !foundControlPlane && !foundProvider && !foundMirror
               ? 0
               : 1;
}
