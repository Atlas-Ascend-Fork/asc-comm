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
#include "hcomm_pod_launch.h"
#include "hcomm_pod_runtime.h"

namespace {
constexpr uint32_t POD_DEFAULT_NOTIFY_WAIT_TIMEOUT = 1836U;

struct PodLaunchState {
    std::vector<ThreadHandle> threads;
    uint32_t notifyWaitTimeout{POD_DEFAULT_NOTIFY_WAIT_TIMEOUT};
    bool batchMode{false};
};

thread_local PodLaunchState g_podLaunchState;
}

void HcommPodRegisterThread(ThreadHandle thread)
{
    if (!g_podLaunchState.batchMode) {
        return;
    }
    if (std::find(g_podLaunchState.threads.begin(), g_podLaunchState.threads.end(), thread) ==
        g_podLaunchState.threads.end()) {
        g_podLaunchState.threads.push_back(thread);
    }
}

bool HcommPodIsBatchLaunchMode()
{
    return g_podLaunchState.batchMode;
}

uint32_t HcommPodGetNotifyWaitTimeout()
{
    return g_podLaunchState.notifyWaitTimeout;
}

void HcommPodSetNotifyWaitTimeout(uint32_t timeout)
{
    g_podLaunchState.notifyWaitTimeout = timeout;
}

void HcommPodSetBatchLaunchMode(bool batchMode)
{
    g_podLaunchState.batchMode = batchMode;
}

HcclResult HcommPodLaunchRegisteredThreads()
{
    if (g_podLaunchState.threads.empty()) {
        return HCCL_SUCCESS;
    }
    return HcommPodTaskLaunch(g_podLaunchState.threads.data(), g_podLaunchState.threads.size());
}

HcclResult HcommPodTryLaunchRegisteredThreads()
{
    if (g_podLaunchState.threads.empty()) {
        return HCCL_SUCCESS;
    }
    return HcommPodTryLaunch(g_podLaunchState.threads.data(), g_podLaunchState.threads.size());
}

void HcommPodClearRegisteredThreads()
{
    g_podLaunchState.threads.clear();
}
