/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "ccu_profiling.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <algorithm>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include "profiling/aprof_pub.h"
#include "runtime/rt_external_base.h"

namespace ccu::runtime::profiling {
namespace {

// Keep the module identifier identical to CCU 1.0 until profiling/tooling
// confirms a separate CCU 2.0 registration contract. Do not allocate one here.
constexpr uint32_t kProfModuleId = 3;
constexpr uint32_t kReportNonPersistent = 1;
constexpr uint32_t kInvalidU32 = 0xFFFFFFFFU;
constexpr uint64_t kInvalidU64 = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint8_t kInvalidU8 = 0xFF;
constexpr uint8_t kWorkFlowModeOpBase = 1;
constexpr uint32_t kCcuTaskInfo = 14;
constexpr uint32_t kApiReady = 1;
constexpr uint32_t kApiUnavailable = 2;

struct ProfApi {
    int32_t (*registerCallback)(uint32_t moduleId, ProfCommandHandle handle) = nullptr;
    int32_t (*registerTypeInfo)(uint16_t level, uint32_t typeId, const char* typeName) = nullptr;
    int32_t (*reportAdditionalInfo)(uint32_t nonPersistent, const MsprofAdditionalInfo* data, uint32_t length) =
        nullptr;
    int32_t (*reportApi)(uint32_t nonPersistent, const MsprofApi* data) = nullptr;
    uint64_t (*str2Id)(const char* hashInfo, size_t length) = nullptr;
    uint64_t (*sysCycleTime)() = nullptr;
};

struct RuntimeApi {
    int32_t (*getTaskIdAndStreamId)(uint32_t* taskId, uint32_t* streamId) = nullptr;
};

struct State {
    std::once_flag initOnce;
    std::atomic<uint32_t> apiState{0}; // 0 uninitialized, 1 ready, 2 unavailable
    std::atomic<bool> l1Enabled{false};
    bool taskInfoTypeRegistered = false;
    std::mutex mutex;

    static State& instance() noexcept
    {
        static State state;
        return state;
    }
};

std::atomic<ProfApi> g_profApi{};
std::atomic<RuntimeApi> g_runtimeApi{};

void logError(const char* format, ...) noexcept
{
    va_list arguments;
    va_start(arguments, format);
    (void)std::fprintf(stderr, "[ccu_runtime] [error] ");
    (void)std::vfprintf(stderr, format, arguments);
    (void)std::fputc('\n', stderr);
    va_end(arguments);
}

bool loadProfApi(ProfApi& api) noexcept
{
    void* handle = dlopen("libprofapi.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr)
        return false;

    api.registerCallback = reinterpret_cast<decltype(api.registerCallback)>(dlsym(handle, "MsprofRegisterCallback"));
    api.registerTypeInfo = reinterpret_cast<decltype(api.registerTypeInfo)>(dlsym(handle, "MsprofRegTypeInfo"));
    api.reportAdditionalInfo =
        reinterpret_cast<decltype(api.reportAdditionalInfo)>(dlsym(handle, "MsprofReportAdditionalInfo"));
    api.reportApi = reinterpret_cast<decltype(api.reportApi)>(dlsym(handle, "MsprofReportApi"));
    api.str2Id = reinterpret_cast<decltype(api.str2Id)>(dlsym(handle, "MsprofStr2Id"));
    api.sysCycleTime = reinterpret_cast<decltype(api.sysCycleTime)>(dlsym(handle, "MsprofSysCycleTime"));
    if (api.registerCallback == nullptr || api.registerTypeInfo == nullptr || api.reportAdditionalInfo == nullptr ||
        api.reportApi == nullptr || api.str2Id == nullptr || api.sysCycleTime == nullptr)
        return false;

    return true;
}

void startSubscribe(uint64_t profSwitch) noexcept
{
    if ((profSwitch & PROF_TASK_TIME_L1_MASK) == 0)
        return;
    const ProfApi api = g_profApi.load(std::memory_order_acquire);
    if (api.registerTypeInfo == nullptr)
        return;
    std::lock_guard<std::mutex> lock(State::instance().mutex);
    if (!State::instance().taskInfoTypeRegistered) {
        if (api.registerTypeInfo(static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL), kCcuTaskInfo, "ccu_task_info") !=
            0)
            return;
        State::instance().taskInfoTypeRegistered = true;
    }
    State::instance().l1Enabled.store(true, std::memory_order_release);
}

void stopSubscribe() noexcept
{
    std::lock_guard<std::mutex> lock(State::instance().mutex);
    State::instance().taskInfoTypeRegistered = false;
    State::instance().l1Enabled.store(false, std::memory_order_release);
}

int32_t handleProfCommand(uint32_t rtType, void* data, uint32_t length) noexcept
{
    (void)length;
    if (data == nullptr || rtType != RT_PROF_CTRL_SWITCH)
        return 1;
    const auto* command = static_cast<const rtProfCommandHandle_t*>(data);
    if (command->type == PROF_COMMANDHANDLE_TYPE_START)
        startSubscribe(command->profSwitch);
    else if (command->type == PROF_COMMANDHANDLE_TYPE_STOP)
        stopSubscribe();
    return 0;
}

bool initializeOnce() noexcept
{
    State& state = State::instance();
    std::call_once(state.initOnce, [&state]() {
        ProfApi api{};
        if (loadProfApi(api)) {
            // Profapi may replay the current START command while registration
            // is in progress. Publish the API table before arming that callback.
            g_profApi.store(api, std::memory_order_release);
        }
        if (g_profApi.load(std::memory_order_acquire).registerCallback != nullptr &&
            api.registerCallback(kProfModuleId, &handleProfCommand) == 0) {
            state.apiState.store(kApiReady, std::memory_order_release);
            return;
        }
        g_profApi.store({}, std::memory_order_release);
        state.apiState.store(kApiUnavailable, std::memory_order_release);
    });
    return state.apiState.load(std::memory_order_acquire) == kApiReady;
}

void getDisplayKernelName(const LaunchContext& context, char (&display)[LaunchContext::maxKernelNameLength]) noexcept
{
    const char* name = context.kernelNameView;
    size_t length = context.kernelNameViewLength;
    if (name != nullptr) {
        const size_t capacity = LaunchContext::maxKernelNameCopyLength;
        length = std::min(length, capacity);
        const void* terminator = std::memchr(name, '\0', length);
        if (terminator != nullptr)
            length = static_cast<size_t>(static_cast<const char*>(terminator) - name);
    } else {
        name = context.kernelName;
        length = strnlen(name, sizeof(context.kernelName));
    }

    if (length == 0) {
        (void)std::memcpy(display, "ccu_kernel", sizeof("ccu_kernel"));
        return;
    }

    const void* parameters = std::memchr(name, '(', length);
    if (parameters != nullptr)
        length = static_cast<const char*>(parameters) - name;

    size_t begin = 0;
    if (length >= 2) {
        for (size_t end = length; end > 2; --end) {
            if (name[end - 1] == ':' && name[end - 2] == ':') {
                begin = end;
                break;
            }
        }
        if (begin == 0 && length >= 2 && name[0] == ':' && name[1] == ':')
            begin = 2;
    }
    length -= begin;
    std::memcpy(display, name + begin, length);
    display[length] = '\0';
}

uint32_t currentThreadId() noexcept
{
#if defined(__NR_gettid)
    return static_cast<uint32_t>(::syscall(__NR_gettid));
#else
    return 0;
#endif
}

bool queryTaskAndStreamId(uint32_t& taskId, uint32_t& streamId) noexcept
{
    RuntimeApi runtimeApi = g_runtimeApi.load(std::memory_order_acquire);
    if (runtimeApi.getTaskIdAndStreamId == nullptr) {
        void* handle = dlopen("libruntime.so", RTLD_NOW | RTLD_NOLOAD);
        if (handle == nullptr)
            handle = dlopen("libruntime.so", RTLD_NOW | RTLD_GLOBAL);
        if (handle == nullptr)
            return false;
        RuntimeApi resolved{};
        resolved.getTaskIdAndStreamId =
            reinterpret_cast<decltype(resolved.getTaskIdAndStreamId)>(dlsym(handle, "rtGetTaskIdAndStreamID"));
        if (resolved.getTaskIdAndStreamId != nullptr) {
            runtimeApi = resolved;
            g_runtimeApi.store(resolved, std::memory_order_release);
        }
        if (runtimeApi.getTaskIdAndStreamId == nullptr)
            return false;
    }

    taskId = kInvalidU32;
    streamId = kInvalidU32;
    return runtimeApi.getTaskIdAndStreamId(&taskId, &streamId) == 0;
}

struct MsprofCcuTaskInfo {
    uint8_t version;
    uint8_t workFlowMode;
    uint64_t itemId;
    uint64_t groupName;
    uint32_t rankId;
    uint32_t rankSize;
    uint16_t streamId;
    uint32_t taskId;
    uint8_t dieId;
    uint8_t missionId;
    uint16_t instrId;
};
static_assert(
    sizeof(MsprofCcuTaskInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH,
    "CCU task profiling payload exceeds MsprofAdditionalInfo");

bool reportNodeApi(const LaunchContext& context, uint32_t threadId, uint64_t itemId) noexcept
{
    MsprofApi report{};
    report.level = static_cast<uint16_t>(MSPROF_REPORT_NODE_LEVEL);
    report.type = MSPROF_REPORT_NODE_LAUNCH_TYPE;
    report.threadId = threadId;
    report.beginTime = context.beginTime;
    report.endTime = context.endTime;
    report.itemId = itemId;
    const ProfApi api = g_profApi.load(std::memory_order_acquire);
    return api.reportApi != nullptr && api.reportApi(kReportNonPersistent, &report) == 0;
}

bool reportLaunchTaskInfo(const LaunchContext& context, uint32_t threadId, uint64_t itemId) noexcept
{
    MsprofAdditionalInfo report{};
    report.level = static_cast<uint16_t>(MSPROF_REPORT_HCCL_NODE_LEVEL);
    report.type = kCcuTaskInfo;
    report.threadId = threadId;
    report.dataLen = sizeof(MsprofCcuTaskInfo);
    report.timeStamp = context.endTime;

    auto* payload = reinterpret_cast<MsprofCcuTaskInfo*>(report.data);
    payload->version = 0;
    payload->workFlowMode = kWorkFlowModeOpBase;
    payload->itemId = itemId;
    payload->groupName = kInvalidU64;
    payload->rankId = context.rankId;
    payload->rankSize = context.rankSize == 0 ? kInvalidU32 : context.rankSize;
    // The legacy type 14 stream field is 16 bits while runtime stream IDs are
    // 32 bits. Preserve the CCU 1.0 narrowing behavior in phase 1.
    payload->streamId = static_cast<uint16_t>(context.streamId);
    payload->taskId = context.taskId;
    payload->dieId = kInvalidU8;
    payload->missionId = kInvalidU8;
    payload->instrId = 0xFFFF;
    const ProfApi api = g_profApi.load(std::memory_order_acquire);
    return api.reportAdditionalInfo != nullptr &&
           api.reportAdditionalInfo(kReportNonPersistent, &report, sizeof(report)) == 0;
}

} // namespace

bool isReportReady() noexcept
{
    // Initialization happens on the first launch, after the runtime has loaded
    // and the profiler can resolve its process-wide dependencies.
    if (!initializeOnce())
        return false;
    return State::instance().apiState.load(std::memory_order_acquire) == 1 &&
           State::instance().l1Enabled.load(std::memory_order_acquire);
}

uint64_t sysCycleTime() noexcept
{
    if (!isReportReady())
        return 0;
    const ProfApi api = g_profApi.load(std::memory_order_acquire);
    return api.sysCycleTime == nullptr ? 0 : api.sysCycleTime();
}

void reportLaunchAfterSubmit(const LaunchContext& context) noexcept
{
    if (!isReportReady())
        return;

    try {
        const uint32_t threadId = currentThreadId();
        char display[LaunchContext::maxKernelNameLength] = {};
        getDisplayKernelName(context, display);
        const ProfApi api = g_profApi.load(std::memory_order_acquire);
        const uint64_t itemId = api.str2Id == nullptr ? kInvalidU64 : api.str2Id(display, std::strlen(display));

        if (!reportNodeApi(context, threadId, itemId))
            logError("CCU profiling node API report failed");

        uint32_t taskId = 0;
        uint32_t streamId = 0;
        if (!queryTaskAndStreamId(taskId, streamId)) {
            logError("CCU profiling task/stream query failed; task info skipped");
            return;
        }

        LaunchContext withTask = context;
        withTask.taskId = taskId;
        withTask.streamId = streamId;
        if (!reportLaunchTaskInfo(withTask, threadId, itemId))
            logError("CCU profiling task info report failed");
    } catch (...) {
        // Profiling must never turn a successful CCU launch into an error.
    }
}

} // namespace ccu::runtime::profiling
