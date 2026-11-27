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
#include <deque>
#include <vector>
#include "asccomm_launch.h"
#include "asccomm_runtime.h"

namespace {
constexpr uint32_t DEFAULT_NOTIFY_WAIT_TIMEOUT = 1836U;
constexpr uint32_t DEFAULT_SQ_FULL_TIMEOUT = 1856U;
constexpr size_t MAX_RETAINED_BATCH_TAGS = 64U;

struct LaunchState {
    std::string batchTag;
    std::unordered_map<std::string, std::vector<ThreadHandle>> taggedThreads;
    std::vector<ThreadHandle>* activeThreads{nullptr};
    std::deque<std::vector<ThreadHandle>> snapshots;
    size_t snapshotDepth{0};
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
    auto& threads = g_launchState.activeThreads ? *g_launchState.activeThreads : g_launchState.threads;
    if (std::find(threads.begin(), threads.end(), thread) == threads.end()) {
        threads.push_back(thread);
    }
}

bool is_default_batch_tag(const char* batchTag) { return batchTag == nullptr || *batchTag == '\0'; }

std::string normalize_batch_tag(const char* batchTag)
{
    return is_default_batch_tag(batchTag) ? std::string{} : std::string(batchTag);
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
    const char* tag = is_default_batch_tag(batchTag) ? "" : batchTag;
    if (g_launchState.batchTag != tag) {
        g_launchState.batchTag = tag;
    }
    g_launchState.activeThreads =
        g_launchState.batchTag.empty() ? &g_launchState.threads : &g_launchState.taggedThreads[g_launchState.batchTag];
    return ASCCOMM_SUCCESS;
}

bool asccomm_is_automatic_launch_mode() { return g_launchState.automaticLaunch && !g_launchState.batchMode; }

namespace {
asccomm_result launch_snapshot(bool tryLaunch)
{
    const auto& registered = g_launchState.activeThreads ? *g_launchState.activeThreads : g_launchState.threads;
    if (registered.empty()) {
        return ASCCOMM_SUCCESS;
    }
    // A recursive submission may register writers. Each nesting level owns a
    // stable snapshot, and deque growth does not invalidate outer references.
    const size_t depth = g_launchState.snapshotDepth;
    if (depth == g_launchState.snapshots.size()) {
        g_launchState.snapshots.emplace_back();
    }
    auto& threads = g_launchState.snapshots[depth];
    threads.assign(registered.begin(), registered.end());
    ++g_launchState.snapshotDepth;
    struct DepthGuard {
        ~DepthGuard() { --g_launchState.snapshotDepth; }
    } guard;
    return tryLaunch ? asccomm_try_launch(threads.data(), static_cast<uint32_t>(threads.size())) :
                       asccomm_task_launch(threads.data(), static_cast<uint32_t>(threads.size()));
}
} // namespace

asccomm_result asccomm_launch_registered_threads() { return launch_snapshot(false); }
asccomm_result asccomm_try_launch_registered_threads() { return launch_snapshot(true); }

asccomm_result asccomm_clear_registered_threads(const char* batchTag)
{
    const char* tag = is_default_batch_tag(batchTag) ? "" : batchTag;
    if (*tag == '\0') {
        g_launchState.threads.clear();
        g_launchState.batchTag.clear();
        g_launchState.activeThreads = &g_launchState.threads;
        return ASCCOMM_SUCCESS;
    }
    auto it = g_launchState.taggedThreads.find(tag);
    if (it != g_launchState.taggedThreads.end()) {
        it->second.clear();
    }
    if (g_launchState.batchTag == tag) {
        g_launchState.batchTag.clear();
        g_launchState.activeThreads = &g_launchState.threads;
    }
    // Bound retained idle tag storage; never evict another pending batch.
    if (g_launchState.taggedThreads.size() > MAX_RETAINED_BATCH_TAGS && it != g_launchState.taggedThreads.end()) {
        g_launchState.taggedThreads.erase(it);
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_clear_active_threads()
{
    // Resolve the non-trivial TLS object once; repeated member expressions can
    // emit repeated dynamic TLS initialization/resolution calls on AICPU.
    auto& state = g_launchState;
    auto& active = state.activeThreads ? *state.activeThreads : state.threads;
    active.clear();
    state.batchTag.clear();
    state.activeThreads = &state.threads;
    return ASCCOMM_SUCCESS;
}
