/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CCU_EXPERIMENTAL_PROFILING_H
#define CCU_EXPERIMENTAL_PROFILING_H

#include <cstdint>

#include "ccu/experimental/ccu_runtime.h"

namespace ccu::runtime::profiling {

struct LaunchContext {
    static constexpr uint32_t maxKernelNameLength = 128;
    static constexpr uint32_t maxKernelNameCopyLength = maxKernelNameLength - 1;

    char kernelName[maxKernelNameLength] = {};
    uint32_t rankId = 0;
    uint32_t rankSize = 0;
    const char* kernelNameView = nullptr;
    uint32_t kernelNameViewLength = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint32_t taskId = 0;
    uint32_t streamId = 0;
};

bool isReportReady() noexcept;
uint64_t sysCycleTime() noexcept;
void reportLaunchAfterSubmit(const LaunchContext& context) noexcept;

} // namespace ccu::runtime::profiling

#endif /* CCU_EXPERIMENTAL_PROFILING_H */
