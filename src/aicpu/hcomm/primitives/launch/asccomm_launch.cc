/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <vector>
#include "asccomm_launch.h"
#include "asccomm_runtime.h"

namespace {
constexpr uint32_t POD_DEFAULT_NOTIFY_WAIT_TIMEOUT = 1836U;
constexpr uint32_t POD_DEFAULT_SQ_FULL_TIMEOUT = 1856U;

struct PodLaunchState {
    std::vector<ThreadHandle> threads;
    uint32_t notifyWaitTimeout{POD_DEFAULT_NOTIFY_WAIT_TIMEOUT};
    uint32_t sqFullTimeout{0};
    bool sqFullTimeoutSet{false};
    bool batchMode{false};
};

thread_local PodLaunchState g_podLaunchState;
}

void asccomm_register_thread(ThreadHandle thread)
{
    if (!g_podLaunchState.batchMode) {
        return;
    }
    if (std::find(g_podLaunchState.threads.begin(), g_podLaunchState.threads.end(), thread) ==
        g_podLaunchState.threads.end()) {
        g_podLaunchState.threads.push_back(thread);
    }
}

bool asccomm_is_batch_launch_mode()
{
    return g_podLaunchState.batchMode;
}

uint32_t asccomm_get_notify_wait_timeout()
{
    return g_podLaunchState.notifyWaitTimeout;
}

void asccomm_set_notify_wait_timeout(uint32_t timeout)
{
    g_podLaunchState.notifyWaitTimeout = timeout;
}

uint32_t asccomm_get_sq_full_timeout()
{
    return g_podLaunchState.sqFullTimeoutSet ? g_podLaunchState.sqFullTimeout : POD_DEFAULT_SQ_FULL_TIMEOUT;
}

void asccomm_set_sq_full_timeout(uint32_t timeout)
{
    g_podLaunchState.sqFullTimeout = timeout;
    g_podLaunchState.sqFullTimeoutSet = true;
}

void asccomm_set_batch_launch_mode(bool batchMode)
{
    g_podLaunchState.batchMode = batchMode;
}

HcclResult asccomm_launch_registered_threads()
{
    if (g_podLaunchState.threads.empty()) {
        return HCCL_SUCCESS;
    }
    return asccomm_task_launch(g_podLaunchState.threads.data(), g_podLaunchState.threads.size());
}

HcclResult asccomm_try_launch_registered_threads()
{
    if (g_podLaunchState.threads.empty()) {
        return HCCL_SUCCESS;
    }
    return asccomm_try_launch(g_podLaunchState.threads.data(), g_podLaunchState.threads.size());
}

void asccomm_clear_registered_threads()
{
    g_podLaunchState.threads.clear();
}
