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
#include <unordered_set>
#include <vector>
#include "asccomm_launch.h"
#include "asccomm_runtime.h"

namespace {
constexpr uint32_t DEFAULT_NOTIFY_WAIT_TIMEOUT = 1836U;
constexpr uint32_t DEFAULT_SQ_FULL_TIMEOUT = 1856U;

struct LaunchState {
    std::string batchTag;
    std::unordered_map<std::string, std::unordered_set<ThreadHandle>> taggedThreads;
    std::vector<ThreadHandle> threads;
    uint32_t notifyWaitTimeout{DEFAULT_NOTIFY_WAIT_TIMEOUT};
    uint32_t sqFullTimeout{0};
    bool sqFullTimeoutSet{false};
    bool batchMode{false};
    bool automaticLaunch{true};
};

thread_local LaunchState g_launchState;
} // namespace

void asccomm_register_thread(ThreadHandle thread)
{
    if (!g_launchState.batchMode) {
        return;
    }
    if (g_launchState.batchTag.empty()) {
        if (std::find(g_launchState.threads.begin(), g_launchState.threads.end(), thread) ==
            g_launchState.threads.end()) {
            g_launchState.threads.push_back(thread);
        }
        return;
    }
    g_launchState.taggedThreads[g_launchState.batchTag].insert(thread);
}

bool is_default_batch_tag(const char* batchTag) { return batchTag == nullptr || *batchTag == '\0'; }

std::string normalize_batch_tag(const char* batchTag)
{
    return is_default_batch_tag(batchTag) ? std::string{} : std::string(batchTag);
}

std::vector<ThreadHandle> get_registered_threads()
{
    if (g_launchState.batchTag.empty()) {
        return g_launchState.threads;
    }
    const auto it = g_launchState.taggedThreads.find(g_launchState.batchTag);
    if (it == g_launchState.taggedThreads.end()) {
        return {};
    }
    return {it->second.begin(), it->second.end()};
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

void asccomm_set_batch_launch_mode(bool batchMode)
{
    g_launchState.batchMode = batchMode;
    g_launchState.automaticLaunch = !batchMode;
}

asccomm_result asccomm_set_batch_launch_mode_with_tag(const char* batchTag, bool batchMode)
{
    g_launchState.batchMode = batchMode;
    g_launchState.automaticLaunch = !batchMode;
    g_launchState.batchTag = normalize_batch_tag(batchTag);
    return ASCCOMM_SUCCESS;
}

bool asccomm_is_automatic_launch_mode() { return g_launchState.automaticLaunch && !g_launchState.batchMode; }

asccomm_result asccomm_launch_registered_threads()
{
    std::vector<ThreadHandle> threads = get_registered_threads();
    if (threads.empty()) {
        return ASCCOMM_SUCCESS;
    }
    return asccomm_task_launch(threads.data(), static_cast<uint32_t>(threads.size()));
}

asccomm_result asccomm_try_launch_registered_threads()
{
    std::vector<ThreadHandle> threads = get_registered_threads();
    if (threads.empty()) {
        return ASCCOMM_SUCCESS;
    }
    return asccomm_try_launch(threads.data(), static_cast<uint32_t>(threads.size()));
}

asccomm_result asccomm_clear_registered_threads(const char* batchTag)
{
    const std::string tag = normalize_batch_tag(batchTag);
    if (tag.empty()) {
        g_launchState.threads.clear();
        g_launchState.batchTag.clear();
        return ASCCOMM_SUCCESS;
    }
    g_launchState.taggedThreads.erase(tag);
    if (g_launchState.batchTag == tag) {
        g_launchState.batchTag.clear();
    }
    return ASCCOMM_SUCCESS;
}
