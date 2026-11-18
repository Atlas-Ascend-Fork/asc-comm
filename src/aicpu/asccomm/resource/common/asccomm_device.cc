/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "asccomm_device.h"
#include "aprof_pub.h"

#include <atomic>
#include <mutex>

namespace {

std::atomic<bool> g_sqe_profiling_enabled{false};

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
constexpr uint32_t ASCCOMM_MSPROF_AICPU_MODULE_ID = 36; // AICPU module ID in log_types.h.
#endif

constexpr uint64_t ASCCOMM_ADPROF_TASK_TIME_L1 = ADPROF_TASK_TIME_L1;

} // namespace

extern "C" __attribute__((weak)) int32_t MsprofRegisterCallback(uint32_t moduleId, ProfCommandHandle handle);

extern "C" int32_t asccomm_handle_profiling_command(uint32_t type, void* data, uint32_t len)
{
    (void)len;
    if (data == nullptr) {
        return -1;
    }
    const auto* command = static_cast<const MsprofCommandHandle*>(data);
    if (type == PROF_COMMANDHANDLE_TYPE_START) {
        asccomm_set_sqe_profiling((command->profSwitch & ASCCOMM_ADPROF_TASK_TIME_L1) != 0);
    } else if (type == PROF_COMMANDHANDLE_TYPE_STOP) {
        asccomm_set_sqe_profiling(false);
    }
    return 0;
}

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
namespace {

std::once_flag g_asccomm_profiling_callback_once;

void asccomm_register_profiling_callback_once()
{
    std::call_once(g_asccomm_profiling_callback_once, []() {
        if (MsprofRegisterCallback != nullptr) {
            (void)MsprofRegisterCallback(ASCCOMM_MSPROF_AICPU_MODULE_ID, asccomm_handle_profiling_command);
        }
    });
}

} // namespace
#endif

extern "C" bool asccomm_is_sqe_profiling_enabled(void)
{
#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
    asccomm_register_profiling_callback_once();
    return g_sqe_profiling_enabled.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

extern "C" void asccomm_set_sqe_profiling(bool enabled) { g_sqe_profiling_enabled = enabled; }
