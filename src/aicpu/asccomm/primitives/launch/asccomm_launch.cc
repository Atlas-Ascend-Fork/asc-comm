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
constexpr uint32_t DEFAULT_NOTIFY_WAIT_TIMEOUT = 1836U;
constexpr uint32_t DEFAULT_SQ_FULL_TIMEOUT = 1856U;

struct LaunchState {
    std::vector<ThreadHandle> threads;
    uint32_t notifyWaitTimeout{DEFAULT_NOTIFY_WAIT_TIMEOUT};
    uint32_t sqFullTimeout{0};
    bool sqFullTimeoutSet{false};
    bool batchMode{false};
};

thread_local LaunchState g_launchState;
} // namespace

void asccomm_register_thread(ThreadHandle thread)
{
    if (!g_launchState.batchMode) {
        return;
    }
    if (std::find(g_launchState.threads.begin(), g_launchState.threads.end(), thread) == g_launchState.threads.end()) {
        g_launchState.threads.push_back(thread);
    }
}

bool asccomm_is_batch_launch_mode() { return g_launchState.batchMode; }

uint32_t asccomm_get_notify_wait_timeout() { return g_launchState.notifyWaitTimeout; }

void asccomm_set_notify_wait_timeout(uint32_t timeout) { g_launchState.notifyWaitTimeout = timeout; }

uint32_t asccomm_get_sq_full_timeout()
{
    return g_launchState.sqFullTimeoutSet ? g_launchState.sqFullTimeout : DEFAULT_SQ_FULL_TIMEOUT;
}

void asccomm_set_sq_full_timeout(uint32_t timeout)
{
    g_launchState.sqFullTimeout = timeout;
    g_launchState.sqFullTimeoutSet = true;
}

void asccomm_set_batch_launch_mode(bool batchMode) { g_launchState.batchMode = batchMode; }

AsccommResult asccomm_launch_registered_threads()
{
    if (g_launchState.threads.empty()) {
        return ASCCOMM_SUCCESS;
    }
    return asccomm_task_launch(g_launchState.threads.data(), g_launchState.threads.size());
}

AsccommResult asccomm_try_launch_registered_threads()
{
    if (g_launchState.threads.empty()) {
        return ASCCOMM_SUCCESS;
    }
    return asccomm_try_launch(g_launchState.threads.data(), g_launchState.threads.size());
}

void asccomm_clear_registered_threads() { g_launchState.threads.clear(); }
