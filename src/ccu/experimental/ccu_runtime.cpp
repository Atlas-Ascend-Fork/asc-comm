/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "detail/ccuobj_reader.h"
#include "ccu_profiling.h"
#include "ccu_runtime_backend.h"
#include "ccu_runtime_external.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rt = ccu::runtime;
using rt::ccu_abort_action_t;
using rt::CCU_ABORT_DEVICE;
using rt::CCU_ABORT_NONE;

struct ccu_external_event_set;
struct ccu_external_xn_set;
using ccu_external_event_set_t = ccu_external_event_set;
using ccu_external_xn_set_t = ccu_external_xn_set;

struct ccu_external_arg_t {
    uint32_t kind;
    uint32_t count;
    void* set;
    uint64_t reserved[2];
};

namespace {

constexpr uint32_t kCcuobjVersion = 1;
constexpr uint32_t kCcuobjAbiRevision = 4;
constexpr const char* kNpuArch = "dav-3510";
constexpr uint64_t kDefaultSyncTimeoutMs = 30000;
constexpr uint64_t kDefaultAbortTimeoutMs = 10000;

template <size_t N>
bool allZero(const uint64_t (&values)[N])
{
    for (uint64_t value : values) {
        if (value != 0)
            return false;
    }
    return true;
}

bool validStatus(ccu_status_t status)
{
    return status == CCU_STATUS_SUCCESS || status == CCU_STATUS_INVALID_ARGUMENT ||
           status == CCU_STATUS_VERSION_MISMATCH || status == CCU_STATUS_INVALID_OBJECT ||
           status == CCU_STATUS_KERNEL_NOT_FOUND || status == CCU_STATUS_SCHEMA_MISMATCH ||
           status == CCU_STATUS_RESOURCE_EXHAUSTED || status == CCU_STATUS_RUNTIME_ERROR ||
           status == CCU_STATUS_TIMEOUT_RECOVERED || status == CCU_STATUS_RECOVERY_FAILED ||
           status == CCU_STATUS_BUSY || status == CCU_STATUS_CONTEXT_POISONED;
}

ccu_status_t normalizeStatus(ccu_status_t status) { return validStatus(status) ? status : CCU_STATUS_RUNTIME_ERROR; }

bool validAbortAction(ccu_abort_action_t action) { return action >= CCU_ABORT_NONE && action <= CCU_ABORT_DEVICE; }

ccu_status_t invalidObject(const char* reason)
{
    const char* diagnostics = std::getenv("CCU_RUNTIME_DIAGNOSTICS");
    if (diagnostics && *diagnostics && std::strcmp(diagnostics, "0") != 0)
        std::fprintf(stderr, "CCUC-RUNTIME: invalid object: %s\n", reason);
    return CCU_STATUS_INVALID_OBJECT;
}

constexpr uint32_t kBitsPerByte = 8;
constexpr size_t kCcuobjHeaderSize = 64;
struct OwnedDescriptor {
    ccu_image_descriptor_t image{};
    ccu_kernel_descriptor_t descriptor{};
    uint32_t selectedKernelIndex = 0;
    std::string target;
    size_t selectedKernelNameOffset = 0;
    uint32_t selectedKernelNameLength = 0;
    std::vector<uint8_t> blob;
    std::vector<uint32_t> kernelIndices;

    void finalizePointers()
    {
        image.target = target.c_str();
        image.ccuobj = blob.data();
        descriptor.image = &image;
        descriptor.kernel_indices = kernelIndices.data();
    }

    const char* selectedKernelName() const
    {
        return selectedKernelNameLength == 0 ? nullptr :
                                               reinterpret_cast<const char*>(blob.data() + selectedKernelNameOffset);
    }
};

struct ValidatedDescriptor {
    ccu::ccuobj::ParsedModule parsed;
    uint32_t selectedKernelIndex = 0;
};

ccu_status_t validateDescriptorHeader(const ccu_kernel_descriptor_t* source)
{
    if (!source || !source->image)
        return invalidObject("descriptor-header");
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateImageHeader(const ccu_image_descriptor_t& image)
{
    if (!image.target || std::strcmp(image.target, kNpuArch) != 0 || !image.ccuobj ||
        image.ccuobj_size < kCcuobjHeaderSize || image.ccuobj_size > static_cast<uint64_t>(SIZE_MAX) ||
        image.ccuobj_size > UINT64_MAX / kBitsPerByte)
        return invalidObject("image-header");
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validatePayload(const ccu_image_descriptor_t& image, ValidatedDescriptor& validated)
{
    if (!ccu::ccuobj::parseCcuobj(image.ccuobj, static_cast<size_t>(image.ccuobj_size), validated.parsed))
        return invalidObject("ccuobj-parse");
    if (validated.parsed.version != kCcuobjVersion || validated.parsed.abiRev != kCcuobjAbiRevision)
        return CCU_STATUS_VERSION_MISMATCH;

    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateDescriptorSchema(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize)
{
    if (source->communicator_size == 0 || source->communicator_size != contextRankSize ||
        contextRank >= contextRankSize || !source->kernel_indices)
        return CCU_STATUS_SCHEMA_MISMATCH;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t validateRanks(const ccu_kernel_descriptor_t* source, uint32_t contextRank, ValidatedDescriptor& validated)
{
    for (uint32_t index = 0; index < source->communicator_size; ++index) {
        const uint32_t kernelIndex = source->kernel_indices[index];
        if (kernelIndex >= validated.parsed.kernels.size())
            return CCU_STATUS_SCHEMA_MISMATCH;

        const ccu::ccuobj::ParsedKernel& kernel = validated.parsed.kernels[kernelIndex];
        if (!kernel.microcode || kernel.instrCount == 0 || kernel.taskArgCount != source->task_arg_count ||
            kernel.channelArgCount > source->communicator_size - 1)
            return CCU_STATUS_SCHEMA_MISMATCH;

        if (index == contextRank)
            validated.selectedKernelIndex = kernelIndex;
    }
    return CCU_STATUS_SUCCESS;
}

void copyOwnedArrays(
    OwnedDescriptor& result, const ccu_kernel_descriptor_t* source, const ccu_image_descriptor_t& image,
    uint32_t selectedKernelIndex)
{
    result.blob.assign(image.ccuobj, image.ccuobj + static_cast<size_t>(image.ccuobj_size));
    result.target = image.target;
    result.image = image;
    result.image.ccuobj_size = result.blob.size();
    result.descriptor = *source;

    result.kernelIndices.assign(source->kernel_indices, source->kernel_indices + source->communicator_size);
    result.selectedKernelIndex = selectedKernelIndex;
}

ccu_status_t makeOwnedDescriptor(
    const ccu_kernel_descriptor_t* source, const ccu_image_descriptor_t& image, const ValidatedDescriptor& validated,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    try {
        auto result = std::make_unique<OwnedDescriptor>();
        copyOwnedArrays(*result, source, image, validated.selectedKernelIndex);
        const ccu::ccuobj::ParsedKernel& parsedKernel = validated.parsed.kernels[validated.selectedKernelIndex];
        if (parsedKernel.name != nullptr) {
            result->selectedKernelNameOffset = static_cast<size_t>(
                reinterpret_cast<const uint8_t*>(parsedKernel.name) - static_cast<const uint8_t*>(image.ccuobj));
            result->selectedKernelNameLength = parsedKernel.nameLen;
        }
        result->finalizePointers();
        owned = std::move(result);
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t copyAndValidateDescriptorImpl(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    ccu_status_t status = validateDescriptorHeader(source);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    const ccu_image_descriptor_t& image = *source->image;
    status = validateImageHeader(image);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    ValidatedDescriptor validated;
    status = validatePayload(image, validated);
    if (status != CCU_STATUS_SUCCESS)
        return status;

    status = validateDescriptorSchema(source, contextRank, contextRankSize);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    status = validateRanks(source, contextRank, validated);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    return makeOwnedDescriptor(source, image, validated, owned);
}

ccu_status_t copyAndValidateDescriptor(
    const ccu_kernel_descriptor_t* source, uint32_t contextRank, uint32_t contextRankSize,
    std::unique_ptr<OwnedDescriptor>& owned)
{
    try {
        return copyAndValidateDescriptorImpl(source, contextRank, contextRankSize, owned);
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

rt::BackendSyncResult normalizeSyncResult(rt::BackendSyncResult result)
{
    if (!validStatus(result.status) || !validAbortAction(result.abortAction))
        return {CCU_STATUS_RECOVERY_FAILED, CCU_ABORT_NONE, false};
    if (result.status == CCU_STATUS_TIMEOUT_RECOVERED && (result.abortAction != CCU_ABORT_DEVICE || !result.terminal))
        return {CCU_STATUS_RECOVERY_FAILED, result.abortAction, false};
    if (result.status == CCU_STATUS_SUCCESS && !result.terminal)
        return {CCU_STATUS_RECOVERY_FAILED, result.abortAction, false};
    if (result.status == CCU_STATUS_RECOVERY_FAILED)
        result.terminal = false;
    return result;
}

} // namespace

struct ccu_kernel;
struct ccu_launch;
struct ccu_external_event_set;
struct ccu_external_xn_set;

struct ccu_context {
    std::mutex mutex;
    rt::Backend* backend = nullptr;
    void* backendContext = nullptr;
    uint32_t rank = 0;
    uint32_t rankSize = 0;
    uint64_t defaultSyncTimeoutMs = 0;
    uint64_t abortTimeoutMs = 0;
    ccu_instance_t boundInstance = 0;
    bool runtimeOwned = false;
    bool hasCachedKernel = false;
    bool poisoned = false;
    ccu_launch* pendingLaunch = nullptr;
    std::unordered_set<ccu_external_event_set*> externalEventSets;
    std::unordered_set<ccu_external_xn_set*> externalXnSets;
};

/* The generated launcher carries the public instance value, while the
 * compiler runtime still owns the context object.  Keep that association in
 * the facade; the backend receives the communicator-owned HComm instance
 * unchanged. */
std::mutex instanceRegistryMutex;
std::unordered_map<ccu_instance_t, ccu_context*> instanceRegistry;

struct ccu_kernel {
    ccu_context* context = nullptr;
    std::unique_ptr<OwnedDescriptor> owned;
};

struct LaunchResult {
    ccu_status_t status = CCU_STATUS_RUNTIME_ERROR;
    ccu_abort_action_t abortAction = CCU_ABORT_NONE;
};

struct ccu_launch {
    ccu_context* context = nullptr;
    ccu_kernel* kernel = nullptr;
    void* backendLaunch = nullptr;
    void* stream = nullptr;
    bool terminal = false;
    bool hasResult = false;
    LaunchResult result{};
};

struct ccu_external_event_set {
    ccu_context* context = nullptr;
    void* backendSet = nullptr;
    uint32_t count = 0;
};

struct ccu_external_xn_set {
    ccu_context* context = nullptr;
    void* backendSet = nullptr;
    uint32_t count = 0;
};

namespace {

ccu_status_t buildBackendExternalArgs(
    ccu_context* context, const ccu_external_arg_t* externalArguments, uint32_t externalArgumentCount,
    std::vector<rt::BackendExternalArg>& backendExternalArguments)
{
    if (externalArgumentCount != 0 && !externalArguments)
        return CCU_STATUS_INVALID_ARGUMENT;
    backendExternalArguments.clear();
    backendExternalArguments.reserve(externalArgumentCount);
    for (uint32_t i = 0; i < externalArgumentCount; ++i) {
        const ccu_external_arg_t& argument = externalArguments[i];
        if (argument.count == 0 || !argument.set || !allZero(argument.reserved))
            return CCU_STATUS_INVALID_ARGUMENT;
        rt::BackendExternalArg backendArgument{};
        backendArgument.kind = argument.kind;
        backendArgument.count = argument.count;
        if (argument.kind == CCU_EXTERNAL_ARG_EVENT) {
            auto* set = static_cast<ccu_external_event_set*>(argument.set);
            if (!set || set->context != context || set->count != argument.count ||
                context->externalEventSets.find(set) == context->externalEventSets.end())
                return CCU_STATUS_INVALID_ARGUMENT;
            backendArgument.set = set->backendSet;
        } else if (argument.kind == CCU_EXTERNAL_ARG_XN) {
            auto* set = static_cast<ccu_external_xn_set*>(argument.set);
            if (!set || set->context != context || set->count != argument.count ||
                context->externalXnSets.find(set) == context->externalXnSets.end())
                return CCU_STATUS_INVALID_ARGUMENT;
            backendArgument.set = set->backendSet;
        } else {
            return CCU_STATUS_INVALID_ARGUMENT;
        }
        if (!backendArgument.set)
            return CCU_STATUS_RUNTIME_ERROR;
        backendExternalArguments.push_back(backendArgument);
    }
    return CCU_STATUS_SUCCESS;
}

ccu_status_t synchronizeLocked(ccu_launch* launch, uint64_t timeoutMs)
{
    if (launch->terminal)
        return launch->hasResult ? launch->result.status : CCU_STATUS_SUCCESS;
    const uint64_t effectiveTimeout = timeoutMs == 0 ? launch->context->defaultSyncTimeoutMs : timeoutMs;
    rt::BackendSyncResult backendResult = normalizeSyncResult(launch->context->backend->synchronize(
        launch->backendLaunch, effectiveTimeout, launch->context->abortTimeoutMs));
    launch->result = {backendResult.status, backendResult.abortAction};
    launch->hasResult = true;
    launch->terminal = backendResult.terminal;
    // HComm's device-abort callback suspends and cleans its communicator. Once
    // device-wide recovery was entered, or recovery could not prove a terminal
    // state, this context is teardown-only and must never submit another mission.
    if (backendResult.abortAction == CCU_ABORT_DEVICE || backendResult.status == CCU_STATUS_RECOVERY_FAILED)
        launch->context->poisoned = true;
    return backendResult.status;
}

struct CleanupResult {
    ccu_status_t status;
    bool released;
};

CleanupResult cleanupLaunchLocked(ccu_launch* launch)
{
    ccu_context* context = launch->context;
    ccu_status_t status = CCU_STATUS_SUCCESS;
    if (!launch->terminal) {
        status = synchronizeLocked(launch, 0);
        if (!launch->terminal)
            return {status, false};
    } else if (launch->hasResult) {
        status = launch->result.status;
    }
    const ccu_status_t releaseStatus = normalizeStatus(context->backend->launchDestroy(launch->backendLaunch));
    if (releaseStatus != CCU_STATUS_SUCCESS)
        return {releaseStatus, false};
    if (context->pendingLaunch == launch)
        context->pendingLaunch = nullptr;
    delete launch->kernel;
    delete launch;
    return {status, true};
}

ccu_status_t getOrCreateInstanceContext(ccu_instance_t instance, ccu_context_t** result)
{
    if (instance == 0 || !result || *result)
        return CCU_STATUS_INVALID_ARGUMENT;
    {
        std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
        auto it = instanceRegistry.find(instance);
        if (it != instanceRegistry.end()) {
            *result = it->second;
            return CCU_STATUS_SUCCESS;
        }
    }

    ccu_context_options_t options{};
    options.struct_size = sizeof(options);
    options.default_sync_timeout_ms = kDefaultSyncTimeoutMs;
    options.abort_timeout_ms = kDefaultAbortTimeoutMs;
    ccu_context_t* context = nullptr;
    ccu_status_t status = ccu_context_create(&options, &context);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    context->runtimeOwned = true;
    ccu_instance_t boundInstance = instance;
    status = ccu_context_bind_instance(context, &boundInstance);
    if (status == CCU_STATUS_BUSY) {
        (void)ccu_context_destroy(&context);
        std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
        auto it = instanceRegistry.find(instance);
        if (it != instanceRegistry.end()) {
            *result = it->second;
            return CCU_STATUS_SUCCESS;
        }
    }
    if (status != CCU_STATUS_SUCCESS) {
        (void)ccu_context_destroy(&context);
        return status;
    }
    *result = context;
    return status;
}

} // namespace

#pragma GCC visibility push(default)
extern "C" {
ccu_status_t ccu_context_create(const ccu_context_options_t* options, ccu_context_t** context)
{
    if (!context || !options || options->struct_size != sizeof(*options) || options->default_sync_timeout_ms == 0 ||
        options->abort_timeout_ms == 0 || !allZero(options->reserved))
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        auto result = std::make_unique<ccu_context>();
        result->backend = &rt::systemBackend();
        result->defaultSyncTimeoutMs = options->default_sync_timeout_ms;
        result->abortTimeoutMs = options->abort_timeout_ms;
        const ccu_status_t status = normalizeStatus(result->backend->contextCreate(
            options->communicator, &result->backendContext, &result->rank, &result->rankSize));
        if (status != CCU_STATUS_SUCCESS) {
            if (result->backendContext &&
                normalizeStatus(result->backend->contextDestroy(result->backendContext)) != CCU_STATUS_SUCCESS)
                return CCU_STATUS_RECOVERY_FAILED;
            return status;
        }
        if (!result->backendContext || result->rankSize == 0 || result->rank >= result->rankSize) {
            if (result->backendContext &&
                normalizeStatus(result->backend->contextDestroy(result->backendContext)) != CCU_STATUS_SUCCESS)
                return CCU_STATUS_RECOVERY_FAILED;
            return CCU_STATUS_RUNTIME_ERROR;
        }
        // Register the profiler callback before applications can submit their
        // first CCU launch, so an earlier profiling-start command is not missed.
        (void)rt::profiling::isReportReady();
        ccu_context* created = result.release();
        {
            std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
            instanceRegistry[reinterpret_cast<ccu_instance_t>(created)] = created;
        }
        *context = created;
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t cleanupPendingLaunchesLocked(ccu_context* context)
{
    ccu_status_t overall = CCU_STATUS_SUCCESS;
    while (context->pendingLaunch) {
        CleanupResult cleanup = cleanupLaunchLocked(context->pendingLaunch);
        if (!cleanup.released)
            return cleanup.status;
        if (overall == CCU_STATUS_SUCCESS && cleanup.status != CCU_STATUS_SUCCESS)
            overall = cleanup.status;
    }
    return overall;
}

void deleteExternalEventSets(ccu_context* context)
{
    while (!context->externalEventSets.empty()) {
        ccu_external_event_set* set = *context->externalEventSets.begin();
        context->externalEventSets.erase(set);
        delete set;
    }
}

void deleteExternalXnSets(ccu_context* context)
{
    while (!context->externalXnSets.empty()) {
        ccu_external_xn_set* set = *context->externalXnSets.begin();
        context->externalXnSets.erase(set);
        delete set;
    }
}

void unregisterContextInstance(ccu_context* context)
{
    std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
    if (context->boundInstance != 0) {
        auto bound = instanceRegistry.find(context->boundInstance);
        if (bound != instanceRegistry.end() && bound->second == context)
            instanceRegistry.erase(bound);
    }
    auto address = instanceRegistry.find(reinterpret_cast<ccu_instance_t>(context));
    if (address != instanceRegistry.end() && address->second == context)
        instanceRegistry.erase(address);
}

ccu_status_t destroyContextLocked(ccu_context* context, std::unique_lock<std::mutex>& lock, ccu_status_t cleanupStatus)
{
    deleteExternalEventSets(context);
    deleteExternalXnSets(context);

    const ccu_status_t destroyStatus = normalizeStatus(context->backend->contextDestroy(context->backendContext));
    if (destroyStatus != CCU_STATUS_SUCCESS)
        return destroyStatus;

    unregisterContextInstance(context);
    lock.unlock();
    delete context;
    return cleanupStatus;
}

ccu_status_t ccu_context_destroy(ccu_context_t** contextPointer)
{
    try {
        if (!contextPointer)
            return CCU_STATUS_INVALID_ARGUMENT;
        ccu_context* context = *contextPointer;
        if (!context)
            return CCU_STATUS_SUCCESS;

        std::unique_lock<std::mutex> lock(context->mutex);
        const ccu_status_t cleanupStatus = cleanupPendingLaunchesLocked(context);
        if (context->pendingLaunch)
            return cleanupStatus;
        *contextPointer = nullptr;
        return destroyContextLocked(context, lock, cleanupStatus);
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_context_external_event_va(
    ccu_context_t* context, uint32_t count, ccu_external_event_set_t** eventSet, uint64_t* vas)
{
    try {
        if (!context || count == 0 || !eventSet || *eventSet || !vas)
            return CCU_STATUS_INVALID_ARGUMENT;
        auto created = std::make_unique<ccu_external_event_set>();
        created->context = context;
        created->count = count;
        std::vector<uint64_t> allocatedVas(count);
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        void* backendSet = nullptr;
        const ccu_status_t status = normalizeStatus(
            context->backend->externalEventVa(context->backendContext, count, &backendSet, allocatedVas.data()));
        if (status != CCU_STATUS_SUCCESS || !backendSet)
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        for (uint32_t i = 0; i < count; ++i)
            if (allocatedVas[i] == 0)
                return CCU_STATUS_RUNTIME_ERROR;
        created->backendSet = backendSet;
        std::copy(allocatedVas.begin(), allocatedVas.end(), vas);
        context->externalEventSets.insert(created.get());
        *eventSet = created.release();
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_context_external_xn_va(
    ccu_context_t* context, uint32_t count, ccu_external_xn_set_t** xnSet, uint64_t* vas)
{
    try {
        if (!context || count == 0 || !xnSet || *xnSet || !vas)
            return CCU_STATUS_INVALID_ARGUMENT;
        auto created = std::make_unique<ccu_external_xn_set>();
        created->context = context;
        created->count = count;
        std::vector<uint64_t> allocatedVas(count);
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        void* backendSet = nullptr;
        const ccu_status_t status = normalizeStatus(
            context->backend->externalXnVa(context->backendContext, count, &backendSet, allocatedVas.data()));
        if (status != CCU_STATUS_SUCCESS || !backendSet)
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        for (uint32_t i = 0; i < count; ++i)
            if (allocatedVas[i] == 0)
                return CCU_STATUS_RUNTIME_ERROR;
        created->backendSet = backendSet;
        std::copy(allocatedVas.begin(), allocatedVas.end(), vas);
        context->externalXnSets.insert(created.get());
        *xnSet = created.release();
        return CCU_STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_compiler_submit_with_schedule(
    ccu_context_t* context, const ccu_kernel_descriptor_t* descriptor, HcomCcuSchd schd, void* stream,
    const uint64_t* arguments, uint32_t argumentCount, const ccu_external_arg_t* externalArguments,
    uint32_t externalArgumentCount)
{
    if (!context || !descriptor || !stream || (argumentCount != 0 && !arguments) ||
        (externalArgumentCount != 0 && !externalArguments))
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(context->mutex);
        if (context->poisoned)
            return CCU_STATUS_CONTEXT_POISONED;
        if (context->pendingLaunch)
            return CCU_STATUS_BUSY;

        std::unique_ptr<OwnedDescriptor> owned;
        ccu_status_t status = copyAndValidateDescriptor(descriptor, context->rank, context->rankSize, owned);
        if (status != CCU_STATUS_SUCCESS)
            return status;
        if (argumentCount != owned->descriptor.task_arg_count)
            return CCU_STATUS_SCHEMA_MISMATCH;
        std::vector<rt::BackendExternalArg> backendExternalArguments;
        status = buildBackendExternalArgs(context, externalArguments, externalArgumentCount, backendExternalArguments);
        if (status != CCU_STATUS_SUCCESS)
            return status;

        auto kernel = std::make_unique<ccu_kernel>();
        kernel->context = context;
        kernel->owned = std::move(owned);
        auto launch = std::make_unique<ccu_launch>();
        launch->context = context;
        launch->kernel = kernel.get();
        launch->stream = stream;
        const bool profilingReady = rt::profiling::isReportReady();
        rt::profiling::LaunchContext profilingContext{};
        if (profilingReady) {
            profilingContext.kernelNameView = kernel->owned->selectedKernelName();
            profilingContext.kernelNameViewLength =
                kernel->owned->selectedKernelNameLength > rt::profiling::LaunchContext::maxKernelNameCopyLength ?
                    rt::profiling::LaunchContext::maxKernelNameCopyLength :
                    kernel->owned->selectedKernelNameLength;
            profilingContext.rankId = context->rank;
            profilingContext.rankSize = context->rankSize;
            profilingContext.beginTime = rt::profiling::sysCycleTime();
        }

        status = normalizeStatus(context->backend->launch(
            context->backendContext, &kernel->owned->descriptor, kernel->owned->selectedKernelIndex, stream, schd,
            arguments, argumentCount, backendExternalArguments.empty() ? nullptr : backendExternalArguments.data(),
            static_cast<uint32_t>(backendExternalArguments.size()), &launch->backendLaunch));
        if (!launch->backendLaunch) {
            return status == CCU_STATUS_SUCCESS ? CCU_STATUS_RUNTIME_ERROR : status;
        }

        if (profilingReady) {
            profilingContext.endTime = rt::profiling::sysCycleTime();
            rt::profiling::reportLaunchAfterSubmit(profilingContext);
        }

        if (schd.cacheTag != 0)
            context->hasCachedKernel = true;
        launch->kernel = kernel.release();
        context->pendingLaunch = launch.release();
        return status;
    } catch (const std::bad_alloc&) {
        return CCU_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

ccu_status_t ccu_compiler_submit(
    ccu_context_t* context, const ccu_kernel_descriptor_t* descriptor, void* stream, const uint64_t* arguments,
    uint32_t argumentCount, const ccu_external_arg_t* externalArguments, uint32_t externalArgumentCount)
{
    return ccu_compiler_submit_with_schedule(
        context, descriptor, HcomCcuSchd{}, stream, arguments, argumentCount, externalArguments, externalArgumentCount);
}

ccu_status_t ccu_context_bind_instance(ccu_context_t* context, ccu_instance_t* instance)
{
    if (!context || !instance || *instance == 0)
        return CCU_STATUS_INVALID_ARGUMENT;
    ccu_instance_t requested = *instance;
    std::lock_guard<std::mutex> contextLock(context->mutex);
    std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
    auto it = instanceRegistry.find(requested);
    if (it != instanceRegistry.end() && it->second != context)
        return CCU_STATUS_BUSY;
    if (context->boundInstance != 0 && context->boundInstance != requested)
        return CCU_STATUS_BUSY;
    const ccu_status_t bindStatus = context->backend->bindInstance(context->backendContext, requested);
    if (bindStatus != CCU_STATUS_SUCCESS)
        return bindStatus;
    instanceRegistry[requested] = context;
    context->boundInstance = requested;
    *instance = requested;
    return CCU_STATUS_SUCCESS;
}

ccu_status_t ccu_compiler_submit_by_instance(
    ccu_instance_t instance, const ccu_kernel_descriptor_t* descriptor, HcomCcuSchd schd, void* stream,
    const uint64_t* arguments, uint32_t argumentCount, const ccu_external_arg_t* externalArguments,
    uint32_t externalArgumentCount)
{
    if (instance == 0)
        return CCU_STATUS_INVALID_ARGUMENT;
    if (schd.rankSize != 0 && schd.rankId >= schd.rankSize)
        return CCU_STATUS_INVALID_ARGUMENT;
    ccu_context_t* context = nullptr;
    ccu_status_t status = getOrCreateInstanceContext(instance, &context);
    if (status != CCU_STATUS_SUCCESS)
        return status;
    // HComm backend contexts start with the single-rank default. The launch
    // schedule is the authoritative source for distributed rank topology.
    if (schd.rankSize != 0) {
        std::lock_guard<std::mutex> lock(context->mutex);
        context->rank = schd.rankId;
        context->rankSize = schd.rankSize;
    } else if (schd.rankId != 0) {
        std::lock_guard<std::mutex> lock(context->mutex);
        if (schd.rankId >= context->rankSize)
            return CCU_STATUS_INVALID_ARGUMENT;
        context->rank = schd.rankId;
    }
    status = ccu_compiler_submit_with_schedule(
        context, descriptor, schd, stream, arguments, argumentCount, externalArguments, externalArgumentCount);
    if (status != CCU_STATUS_SUCCESS) {
        std::unique_lock<std::mutex> lock(context->mutex);
        const ccu_status_t cleanupStatus = cleanupPendingLaunchesLocked(context);
        const bool idle = context->pendingLaunch == nullptr;
        const bool poisoned = context->poisoned;
        lock.unlock();
        if (idle && !poisoned && context->runtimeOwned && !context->hasCachedKernel)
            (void)ccu_context_destroy(&context);
        if (cleanupStatus != CCU_STATUS_SUCCESS)
            return cleanupStatus;
    }
    return status;
}

ccu_status_t ccu_synchronize(ccu_instance_t instance, void* stream, uint64_t timeoutMs)
{
    if (instance == 0)
        return CCU_STATUS_INVALID_ARGUMENT;
    ccu_context_t* context = nullptr;
    {
        std::lock_guard<std::mutex> instanceLock(instanceRegistryMutex);
        auto it = instanceRegistry.find(instance);
        if (it == instanceRegistry.end())
            return CCU_STATUS_SUCCESS;
        context = it->second;
    }
    if (!context)
        return CCU_STATUS_SUCCESS;
    if (!stream)
        return CCU_STATUS_INVALID_ARGUMENT;
    try {
        std::unique_lock<std::mutex> lock(context->mutex);
        if (!context->pendingLaunch) {
            if (context->poisoned)
                return CCU_STATUS_CONTEXT_POISONED;
            if (context->runtimeOwned && !context->hasCachedKernel)
                return destroyContextLocked(context, lock, CCU_STATUS_SUCCESS);
            return CCU_STATUS_SUCCESS;
        }
        if (context->pendingLaunch->stream != stream)
            return CCU_STATUS_INVALID_ARGUMENT;

        ccu_launch* launch = context->pendingLaunch;
        const ccu_status_t syncStatus = synchronizeLocked(launch, timeoutMs);
        if (!launch->terminal)
            return syncStatus;
        const CleanupResult cleanup = cleanupLaunchLocked(launch);
        if (!cleanup.released)
            return cleanup.status;
        if (context->poisoned)
            return syncStatus;
        if (context->runtimeOwned && !context->hasCachedKernel)
            return destroyContextLocked(context, lock, syncStatus);
        return syncStatus;
    } catch (...) {
        return CCU_STATUS_RUNTIME_ERROR;
    }
}

const char* ccu_status_string(ccu_status_t status)
{
    if (status == CCU_STATUS_SUCCESS) {
        return "success";
    }
    if (status == CCU_STATUS_INVALID_ARGUMENT) {
        return "invalid argument";
    }
    if (status == CCU_STATUS_VERSION_MISMATCH) {
        return "version mismatch";
    }
    if (status == CCU_STATUS_INVALID_OBJECT) {
        return "invalid object";
    }
    if (status == CCU_STATUS_KERNEL_NOT_FOUND) {
        return "kernel not found";
    }
    if (status == CCU_STATUS_SCHEMA_MISMATCH) {
        return "schema mismatch";
    }
    if (status == CCU_STATUS_RESOURCE_EXHAUSTED) {
        return "resource exhausted";
    }
    if (status == CCU_STATUS_RUNTIME_ERROR) {
        return "runtime error";
    }
    if (status == CCU_STATUS_TIMEOUT_RECOVERED) {
        return "timeout recovered";
    }
    if (status == CCU_STATUS_RECOVERY_FAILED) {
        return "recovery failed";
    }
    if (status == CCU_STATUS_BUSY) {
        return "busy";
    }
    if (status == CCU_STATUS_CONTEXT_POISONED) {
        return "context poisoned";
    }
    return "unknown status";
}

} // extern "C"
#pragma GCC visibility pop
