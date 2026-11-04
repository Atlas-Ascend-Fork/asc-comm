/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "asccomm_cpu_primitives.h"

#include <dlfcn.h>

#include <cstddef>

namespace {

constexpr int32_t ASCCOMM_CPU_E_PARAM = 1;
constexpr int32_t ASCCOMM_CPU_E_NOT_SUPPORT = 2;

using NotifyRecordFunc = int32_t (*)(void* notify, void* stream);
using NotifyWaitFunc = int32_t (*)(void* notify, void* stream, uint32_t timeout);

struct RuntimeNotifyApi {
    NotifyRecordFunc record;
    NotifyWaitFunc wait;
};

RuntimeNotifyApi LoadRuntimeNotifyApi()
{
    RuntimeNotifyApi api{};
    void* handle = dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        return api;
    }
    api.record = reinterpret_cast<NotifyRecordFunc>(dlsym(handle, "rtsNotifyRecord"));
    api.wait = reinterpret_cast<NotifyWaitFunc>(dlsym(handle, "rtsNotifyWaitAndReset"));
    return api;
}

const RuntimeNotifyApi& GetRuntimeNotifyApi()
{
    static const RuntimeNotifyApi api = LoadRuntimeNotifyApi();
    return api;
}

// The offsets mirror hccl::CpuTsThread and hccl::AicpuTsThread in CANN 9.2.0.
// hcomm owns these layouts; asc-comm intentionally consumes existing ThreadHandles
// without an adapter.
constexpr size_t CPU_THREAD_NOTIFY_NUM_OFFSET = 120;
constexpr size_t CPU_THREAD_STREAM_OFFSET = 136;
constexpr size_t CPU_THREAD_NOTIFYS_OFFSET = 144;
constexpr size_t AICPU_THREAD_NOTIFY_NUM_OFFSET = 108;
constexpr size_t AICPU_THREAD_DEV_ID_OFFSET = 116;
constexpr size_t AICPU_THREAD_NOTIFYS_OFFSET = 128;
constexpr size_t LOCAL_NOTIFY_PTR_OFFSET = 24;
constexpr size_t STREAM_PTR_OFFSET = 8;
constexpr size_t VECTOR_DATA_OFFSET = 0;
constexpr size_t VECTOR_SIZE_OFFSET = 8;

// Reject corrupted object layouts where the notify vector reports an implausible size.
constexpr uint32_t MAX_THREAD_NOTIFY_NUM = 1U << 28U;

template <typename T>
T ReadField(uint64_t object, size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

struct CpuThreadView {
    void* stream;
    void** notifyObjects;
    uint64_t notifyNum;
};

bool GetCpuThreadView(uint64_t thread, CpuThreadView& view)
{
    if (thread == 0) {
        return false;
    }
    const uint32_t notifyNum = ReadField<uint32_t>(thread, CPU_THREAD_NOTIFY_NUM_OFFSET);
    if (notifyNum == 0 || notifyNum > MAX_THREAD_NOTIFY_NUM) {
        return false;
    }

    const uintptr_t streamObject = ReadField<uintptr_t>(thread, CPU_THREAD_STREAM_OFFSET);
    if (streamObject == 0) {
        return false;
    }
    void* stream = reinterpret_cast<void*>(ReadField<uintptr_t>(streamObject, STREAM_PTR_OFFSET));

    void** notifyObjects = reinterpret_cast<void**>(ReadField<uintptr_t>(thread, CPU_THREAD_NOTIFYS_OFFSET));
    if (stream == nullptr || notifyObjects == nullptr) {
        return false;
    }

    view.stream = stream;
    view.notifyObjects = notifyObjects;
    view.notifyNum = notifyNum;
    return true;
}

bool GetNotifyObjects(
    uint64_t thread, size_t notifyNumOffset, size_t notifysOffset, void**& notifyObjects, uint64_t& notifyNum)
{
    if (thread == 0) {
        return false;
    }
    const uint32_t count = ReadField<uint32_t>(thread, notifyNumOffset);
    if (count == 0 || count > MAX_THREAD_NOTIFY_NUM) {
        return false;
    }
    notifyObjects = reinterpret_cast<void**>(ReadField<uintptr_t>(thread, notifysOffset));
    if (notifyObjects == nullptr) {
        return false;
    }
    notifyNum = count;
    return true;
}

void* GetCpuNotify(uint64_t thread, uint32_t notifyIdx)
{
    CpuThreadView view{};
    if (!GetCpuThreadView(thread, view) || notifyIdx >= view.notifyNum) {
        return nullptr;
    }
    const uintptr_t notifyObject = reinterpret_cast<uintptr_t>(view.notifyObjects[notifyIdx]);
    if (notifyObject == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ReadField<uintptr_t>(notifyObject, LOCAL_NOTIFY_PTR_OFFSET));
}

void* GetAicpuNotify(uint64_t thread, uint32_t notifyIdx)
{
    void** notifyObjects = nullptr;
    uint64_t notifyNum = 0;
    if (!GetNotifyObjects(
            thread, AICPU_THREAD_NOTIFY_NUM_OFFSET, AICPU_THREAD_NOTIFYS_OFFSET, notifyObjects, notifyNum) ||
        notifyIdx >= notifyNum) {
        return nullptr;
    }
    const uintptr_t notifyObject = reinterpret_cast<uintptr_t>(notifyObjects[notifyIdx]);
    if (notifyObject == 0) {
        return nullptr;
    }
    return reinterpret_cast<void*>(ReadField<uintptr_t>(notifyObject, LOCAL_NOTIFY_PTR_OFFSET));
}

} // namespace

extern "C" int32_t asccomm_cpu_thread_notify_record_on_thread(
    uint64_t thread, uint64_t dstThread, uint32_t dstNotifyIdx)
{
    CpuThreadView source{};
    if (!GetCpuThreadView(thread, source)) {
        return ASCCOMM_CPU_E_PARAM;
    }

    void* notify = GetAicpuNotify(dstThread, dstNotifyIdx);
    if (notify == nullptr) {
        return ASCCOMM_CPU_E_PARAM;
    }

    const RuntimeNotifyApi& api = GetRuntimeNotifyApi();
    if (api.record == nullptr) {
        return ASCCOMM_CPU_E_NOT_SUPPORT;
    }
    return api.record(notify, source.stream);
}

extern "C" int32_t asccomm_cpu_thread_notify_wait_on_thread(uint64_t thread, uint32_t notifyIdx, uint32_t timeOut)
{
    void* notify = GetCpuNotify(thread, notifyIdx);
    if (notify == nullptr) {
        return ASCCOMM_CPU_E_PARAM;
    }

    CpuThreadView view{};
    if (!GetCpuThreadView(thread, view)) {
        return ASCCOMM_CPU_E_PARAM;
    }

    const RuntimeNotifyApi& api = GetRuntimeNotifyApi();
    if (api.wait == nullptr) {
        return ASCCOMM_CPU_E_NOT_SUPPORT;
    }
    return api.wait(notify, view.stream, timeOut);
}
