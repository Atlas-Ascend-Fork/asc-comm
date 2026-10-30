/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_runtime_hcomm_backend.h"

#include "ccu_aot_adapter.h"
#include "ccu_aot_log.h"
#include "ccu_runtime_external.h"

#include <memory>
#include <new>
#include <vector>

namespace ccu::runtime {
namespace {

struct HcommContext;

struct ExternalEventSet {
    HcommContext* context = nullptr;
    uint64_t handle = 0;
    uint32_t count = 0;
};

struct ExternalXnSet {
    HcommContext* context = nullptr;
    uint64_t handle = 0;
    uint32_t count = 0;
};

struct HcommLaunch {
    HcommContext* context = nullptr;
    uint32_t kernelId = UINT32_MAX;
    void* stream = nullptr;
    bool submitFailed = false;
    bool terminal = false;
    bool recoveryPending = false;
    uint32_t recoveryRuntimeCode = 0;
    BackendSyncResult result{};
};

struct HcommContext {
    CcuAotContext* runtime = nullptr;
    HcommLaunch* activeLaunch = nullptr;
    uint32_t rank = 0;
    uint32_t rankSize = 1;
    std::vector<ExternalEventSet*> externalEventSets;
    std::vector<ExternalXnSet*> externalXnSets;
};

ccu_status_t mapCoreError(CcuAotError error)
{
    switch (error) {
        case CCU_AOT_SUCCESS:
            return CCU_STATUS_SUCCESS;
        case CCU_AOT_E_PARAM:
            return CCU_STATUS_INVALID_ARGUMENT;
        case CCU_AOT_E_REGISTER:
            return CCU_STATUS_RESOURCE_EXHAUSTED;
        case CCU_AOT_E_INIT:
        case CCU_AOT_E_LAUNCH:
        case CCU_AOT_E_NOT_SUBMITTED:
            return CCU_STATUS_RUNTIME_ERROR;
    }
    return CCU_STATUS_RUNTIME_ERROR;
}

ccu_status_t validateLaunchArguments(
    HcommContext* context, const ccu_kernel_descriptor_t* descriptor, const ccu_rank_entry_t* rankEntry, void* stream,
    void** launchHandle, const ccu_task_arg_t* arguments, uint32_t argumentCount,
    const BackendExternalArg* externalArguments, uint32_t externalArgumentCount)
{
    if (!context || !descriptor || !descriptor->image || !rankEntry || !stream || !launchHandle || *launchHandle ||
        (argumentCount != 0 && !arguments) || (externalArgumentCount != 0 && !externalArguments))
        return CCU_STATUS_INVALID_ARGUMENT;
    if (context->activeLaunch)
        return CCU_STATUS_BUSY;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t bindLaunchChannels(HcommContext* context, HcomCcuSchd schd)
{
    if (schd.channelSize != 0 && !schd.channelList)
        return CCU_STATUS_INVALID_ARGUMENT;
    if (!schd.channelList)
        return CCU_STATUS_SUCCESS;
    if (schd.channelSize > UINT32_MAX)
        return CCU_STATUS_INVALID_ARGUMENT;

    const auto* channelList = static_cast<const uint64_t*>(schd.channelList);
    return mapCoreError(ccu_aot_bind_channels(context->runtime, channelList, static_cast<uint32_t>(schd.channelSize)));
}

std::vector<uint64_t> collectTaskArguments(const ccu_task_arg_t* arguments, uint32_t argumentCount)
{
    std::vector<uint64_t> taskArguments;
    taskArguments.reserve(argumentCount);
    for (uint32_t index = 0; index < argumentCount; ++index)
        taskArguments.push_back(arguments[index].bits);
    return taskArguments;
}

ccu_status_t convertExternalArgument(HcommContext* context, const BackendExternalArg& arg, CcuAotExternalArg& converted)
{
    if (arg.count == 0 || !arg.set)
        return CCU_STATUS_INVALID_ARGUMENT;

    converted.kind = arg.kind;
    converted.count = arg.count;
    if (arg.kind == CCU_EXTERNAL_ARG_EVENT) {
        auto* set = static_cast<ExternalEventSet*>(arg.set);
        if (!set || set->context != context || set->count != arg.count)
            return CCU_STATUS_INVALID_ARGUMENT;
        converted.acquire_handle = set->handle;
        return CCU_STATUS_SUCCESS;
    }

    if (arg.kind == CCU_EXTERNAL_ARG_XN) {
        auto* set = static_cast<ExternalXnSet*>(arg.set);
        if (!set || set->context != context || set->count != arg.count)
            return CCU_STATUS_INVALID_ARGUMENT;
        converted.acquire_handle = set->handle;
        return CCU_STATUS_SUCCESS;
    }
    return CCU_STATUS_INVALID_ARGUMENT;
}

ccu_status_t convertExternalArguments(
    HcommContext* context, const BackendExternalArg* arguments, uint32_t argumentCount,
    std::vector<CcuAotExternalArg>& converted)
{
    converted.reserve(argumentCount);
    for (uint32_t index = 0; index < argumentCount; ++index) {
        CcuAotExternalArg aotArgument{};
        const ccu_status_t status = convertExternalArgument(context, arguments[index], aotArgument);
        if (status != CCU_STATUS_SUCCESS)
            return status;
        converted.push_back(aotArgument);
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t registerKernel(
    HcommContext* context, const ccu_kernel_descriptor_t* descriptor, const ccu_rank_entry_t* rankEntry,
    const std::vector<CcuAotExternalArg>& externalArguments, uint32_t& kernelId)
{
    CcuAotError error = ccu_aot_register_kernel_with_external(
        context->runtime, descriptor->image->ccuobj, descriptor->image->ccuobj_size, rankEntry->kernel_index,
        externalArguments.empty() ? nullptr : externalArguments.data(), static_cast<uint32_t>(externalArguments.size()),
        &kernelId);
    return mapCoreError(error);
}

ccu_status_t submitRegisteredKernel(
    HcommContext* context, uint32_t kernelId, const std::vector<uint64_t>& taskArguments, uint32_t argumentCount,
    const std::vector<CcuAotExternalArg>& externalArguments, void* stream, bool& submitFailed)
{
    CcuAotError error = ccu_aot_launch_registered_on_stream(
        context->runtime, kernelId, 0, taskArguments.empty() ? nullptr : taskArguments.data(), argumentCount,
        externalArguments.empty() ? nullptr : externalArguments.data(), static_cast<uint32_t>(externalArguments.size()),
        stream);
    if (error == CCU_AOT_E_NOT_SUBMITTED) {
        CcuAotError releaseError = ccu_aot_unregister_kernel(context->runtime, kernelId);
        return releaseError == CCU_AOT_SUCCESS ? mapCoreError(error) : mapCoreError(releaseError);
    }
    submitFailed = error != CCU_AOT_SUCCESS;
    return CCU_STATUS_SUCCESS;
}

class HcommBackend final : public Backend {
public:
    ccu_status_t contextCreate(void* communicator, void** context, uint32_t* rank, uint32_t* rankSize) noexcept override
    {
        (void)communicator;
        if (!context || !rank || !rankSize || *context)
            return CCU_STATUS_INVALID_ARGUMENT;
        try {
            auto created = std::make_unique<HcommContext>();
            CcuAotError error = ccu_aot_attach_current(&created->runtime, rank, rankSize);
            if (error != CCU_AOT_SUCCESS) {
                CCU_AOT_LOGE(
                    "context create attach failed: error=%d communicator=%p\n", static_cast<int>(error), communicator);
                return mapCoreError(error);
            }
            *context = created.release();
            return CCU_STATUS_SUCCESS;
        } catch (const std::bad_alloc&) {
            return CCU_STATUS_RESOURCE_EXHAUSTED;
        } catch (...) {
            return CCU_STATUS_RUNTIME_ERROR;
        }
    }

    ccu_status_t contextDestroy(void* context) noexcept override
    {
        auto* hcommContext = static_cast<HcommContext*>(context);
        if (!hcommContext || hcommContext->activeLaunch)
            return hcommContext ? CCU_STATUS_BUSY : CCU_STATUS_INVALID_ARGUMENT;
        CcuAotError error = ccu_aot_finalize(hcommContext->runtime);
        if (error != CCU_AOT_SUCCESS)
            return mapCoreError(error);
        for (ExternalEventSet* set : hcommContext->externalEventSets)
            delete set;
        for (ExternalXnSet* set : hcommContext->externalXnSets)
            delete set;
        delete hcommContext;
        return CCU_STATUS_SUCCESS;
    }

    ccu_status_t bindInstance(void* context, ccu_instance_t instance) noexcept override
    {
        auto* hcommContext = static_cast<HcommContext*>(context);
        if (!hcommContext || instance == 0)
            return CCU_STATUS_INVALID_ARGUMENT;
        return mapCoreError(ccu_aot_bind_instance(hcommContext->runtime, 0, static_cast<uint64_t>(instance)));
    }

    ccu_status_t externalEventVa(void* context, uint32_t count, void** eventSet, uint64_t* vas) noexcept override
    {
        (void)context;
        (void)count;
        (void)eventSet;
        (void)vas;
        return CCU_STATUS_INVALID_ARGUMENT;
    }

    ccu_status_t externalXnVa(void* context, uint32_t count, void** xnSet, uint64_t* vas) noexcept override
    {
        (void)context;
        (void)count;
        (void)xnSet;
        (void)vas;
        return CCU_STATUS_INVALID_ARGUMENT;
    }

    ccu_status_t launch(
        void* context, const ccu_kernel_descriptor_t* descriptor, const ccu_rank_entry_t* rankEntry, void* stream,
        HcomCcuSchd schd, const ccu_task_arg_t* arguments, uint32_t argumentCount,
        const BackendExternalArg* externalArguments, uint32_t externalArgumentCount,
        void** launchHandle) noexcept override
    {
        auto* hcommContext = static_cast<HcommContext*>(context);
        const ccu_status_t argumentStatus = validateLaunchArguments(
            hcommContext, descriptor, rankEntry, stream, launchHandle, arguments, argumentCount, externalArguments,
            externalArgumentCount);
        if (argumentStatus != CCU_STATUS_SUCCESS)
            return argumentStatus;

        if (schd.rankSize != 0) {
            if (schd.rankId >= schd.rankSize)
                return CCU_STATUS_INVALID_ARGUMENT;
            hcommContext->rank = schd.rankId;
            hcommContext->rankSize = schd.rankSize;
        } else if (schd.rankId != 0) {
            if (schd.rankId >= hcommContext->rankSize)
                return CCU_STATUS_INVALID_ARGUMENT;
            hcommContext->rank = schd.rankId;
        }
        const CcuAotError topologyError =
            ccu_aot_bind_topology(hcommContext->runtime, hcommContext->rank, hcommContext->rankSize);
        if (topologyError != CCU_AOT_SUCCESS)
            return mapCoreError(topologyError);

        const ccu_status_t bindStatus = bindLaunchChannels(hcommContext, schd);
        if (bindStatus != CCU_STATUS_SUCCESS)
            return bindStatus;

        try {
            auto created = std::make_unique<HcommLaunch>();
            created->context = hcommContext;
            const std::vector<uint64_t> taskArguments = collectTaskArguments(arguments, argumentCount);
            std::vector<CcuAotExternalArg> externalAotArguments;
            const ccu_status_t externalStatus =
                convertExternalArguments(hcommContext, externalArguments, externalArgumentCount, externalAotArguments);
            if (externalStatus != CCU_STATUS_SUCCESS)
                return externalStatus;

            const ccu_status_t registerStatus =
                registerKernel(hcommContext, descriptor, rankEntry, externalAotArguments, created->kernelId);
            if (registerStatus != CCU_STATUS_SUCCESS)
                return registerStatus;

            const ccu_status_t submitStatus = submitRegisteredKernel(
                hcommContext, created->kernelId, taskArguments, argumentCount, externalAotArguments, stream,
                created->submitFailed);
            if (submitStatus != CCU_STATUS_SUCCESS)
                return submitStatus;

            created->stream = stream;
            hcommContext->activeLaunch = created.get();
            *launchHandle = created.release();
            // Keep the launch handle alive after a post-submit error so the public
            // synchronize path can perform the bounded recovery transaction.
            return CCU_STATUS_SUCCESS;
        } catch (const std::bad_alloc&) {
            return CCU_STATUS_RESOURCE_EXHAUSTED;
        } catch (...) {
            return CCU_STATUS_RUNTIME_ERROR;
        }
    }

    BackendSyncResult synchronize(void* launchHandle, uint64_t syncTimeoutMs, uint64_t abortTimeoutMs) noexcept override
    {
        auto* launch = static_cast<HcommLaunch*>(launchHandle);
        if (!launch)
            return {CCU_STATUS_INVALID_ARGUMENT, CCU_ABORT_NONE, false};
        if (!launch->terminal) {
            const bool continuingDeviceRecovery = launch->recoveryPending;
            CcuAotSyncResult coreResult{};
            (void)ccu_aot_sync_ex_on_stream(
                launch->context->runtime, 0, launch->stream, syncTimeoutMs, abortTimeoutMs,
                continuingDeviceRecovery ? 1u : 0u, &coreResult);
            if (coreResult.abortAction == CCU_AOT_ABORT_DEVICE && launch->recoveryRuntimeCode == 0)
                launch->recoveryRuntimeCode = coreResult.runtimeCode;
            const bool deviceRecovery = continuingDeviceRecovery || coreResult.abortAction == CCU_AOT_ABORT_DEVICE;
            ccu_status_t status = CCU_STATUS_RUNTIME_ERROR;
            if (coreResult.terminal) {
                if (deviceRecovery && (coreResult.status != CCU_AOT_SUCCESS || continuingDeviceRecovery))
                    status = CCU_STATUS_TIMEOUT_RECOVERED;
                else if (launch->submitFailed)
                    status = CCU_STATUS_RUNTIME_ERROR;
                else if (coreResult.status == CCU_AOT_SUCCESS)
                    status = CCU_STATUS_SUCCESS;
            } else {
                status = CCU_STATUS_RECOVERY_FAILED;
            }
            launch->result = {
                status, deviceRecovery ? CCU_ABORT_DEVICE : static_cast<ccu_abort_action_t>(coreResult.abortAction),
                coreResult.terminal != 0};
            launch->terminal = coreResult.terminal != 0;
            launch->recoveryPending = deviceRecovery && !launch->terminal;
        }
        return launch->result;
    }

    ccu_status_t launchDestroy(void* launchHandle) noexcept override
    {
        auto* launch = static_cast<HcommLaunch*>(launchHandle);
        if (!launch)
            return CCU_STATUS_INVALID_ARGUMENT;
        if (!launch->terminal)
            return CCU_STATUS_BUSY;
        CcuAotError error = ccu_aot_unregister_kernel(launch->context->runtime, launch->kernelId);
        if (error != CCU_AOT_SUCCESS)
            return mapCoreError(error);
        launch->context->activeLaunch = nullptr;
        delete launch;
        return CCU_STATUS_SUCCESS;
    }
};

} // namespace

Backend& hcommBackend() noexcept
{
    static HcommBackend backend;
    return backend;
}

} // namespace ccu::runtime
