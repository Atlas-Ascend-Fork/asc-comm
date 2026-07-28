/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "launch_context.h"

constexpr u32 THREAD_VECTOR_DEFAULT_SIZE = 128; // 设置vector初始长度，避免频繁扩容
constexpr u32 NOTIFY_WAIT_TIMEOUT_OFFSET = 50; // AICPU device侧notify等待超时偏移量

extern HcclResult CommTaskLaunch(ThreadHandle *threads, uint32_t threadNum);
extern HcclResult DispatchAllStreams(ThreadHandle *threads, uint32_t threadNum);

LaunchContext::LaunchContext()
{
    threadVec_.reserve(THREAD_VECTOR_DEFAULT_SIZE);
}

HcclResult LaunchContext::HandleEagerMode()
{
    // 带launchTag部分
    if (!launchModeMap_.empty()) {
        auto it = launchModeMap_.find(launchTag_);
        if (it != launchModeMap_.end()) {
            std::vector<ThreadHandle> threadVec(it->second.begin(), it->second.end());
            CHK_RET(CommTaskLaunch(threadVec.data(), threadVec.size()));
            HCCL_INFO("[%s]success, launchTag[%s], size[%u]", __func__, launchTag_.c_str(), threadVec.size());
        }
    }

    // 不带launchTag部分
    if (!threadVec_.empty()) {
        CHK_RET(CommTaskLaunch(threadVec_.data(), threadVec_.size()));
        HCCL_INFO("[%s]success, size[%u]", __func__, threadVec_.size());
    }
    return HCCL_SUCCESS;
}

HcclResult LaunchContext::HandleDispatchAllStreams()
{
    // 带launchTag部分
    if (!launchModeMap_.empty()) {
        auto it = launchModeMap_.find(launchTag_);
        if (it != launchModeMap_.end()) {
            std::vector<ThreadHandle> threadVec(it->second.begin(), it->second.end());
            CHK_RET(DispatchAllStreams(threadVec.data(), threadVec.size()));
        }
    }

    // 不带launchTag部分
    if (!threadVec_.empty()) {
        CHK_RET(DispatchAllStreams(threadVec_.data(), threadVec_.size()));
    }
    return HCCL_SUCCESS;
}

HcclResult LaunchContext::HandleClear()
{
    threadVec_.clear();
    if (!launchModeMap_.empty()) {
        launchModeMap_.erase(launchTag_);
    }
    HCCL_INFO("[%s] begin clear, launchTag[%s], launchMode[%d].",
        __func__, launchTag_.c_str(), static_cast<int32_t>(mode_));

    return HCCL_SUCCESS;
}

HcclResult LaunchContext::SetNotifyWaitTimeOut(uint32_t timeout)
{
    notifyWaitTimeoutConfig_.notifyWaitTimeout = timeout;
    notifyWaitTimeoutConfig_.isSet = true;
    return HCCL_SUCCESS;
}

HcclResult LaunchContext::GetNotifyWaitTimeOut(uint32_t& timeout)
{
    timeout = notifyWaitTimeoutConfig_.notifyWaitTimeout;
#ifndef CCL_KERNEL_AICPU
    if (!notifyWaitTimeoutConfig_.isSet) {
        timeout = timeout + NOTIFY_WAIT_TIMEOUT_OFFSET;
    }
#endif
    return HCCL_SUCCESS;
}

HcclResult LaunchContext::SetSqFullTimeOut(uint32_t timeout)
{
    sqFullTimeoutConfig_.sqFullTimeout = timeout;
    sqFullTimeoutConfig_.isSet = true;
    return HCCL_SUCCESS;
}

uint32_t LaunchContext::GetSqFullTimeOut()
{
    return sqFullTimeoutConfig_.sqFullTimeout;
}

HcclResult LaunchContext::SetLaunchMode(const char* launchTag, HcommLaunchMode mode)
{
    mode_ = mode;
    // 统一处理 launchTag
    bool defaultTag = (launchTag == nullptr);
    launchTag_ = defaultTag ? "" : std::string(launchTag);
    HCCL_INFO("[%s] SetLaunchMode begin, launchTag[%s], launchMode[%d].",
        __func__, launchTag_.c_str(), static_cast<int32_t>(mode));

    switch (mode_) {
        case HCOMM_LAUNCH_MODE_BATCH:
            return HCCL_SUCCESS;
        case HCOMM_LAUNCH_MODE_EAGER:
            CHK_RET(HandleEagerMode());
            // 缺省 tag 模式下清理缓存
            return HandleClear();
        case HCOMM_LAUNCH_MODE_RESERVED:
            if (!defaultTag) {
                return HandleClear();
            }
            return HCCL_SUCCESS;
        default:
            return HCCL_SUCCESS;
    }
}
