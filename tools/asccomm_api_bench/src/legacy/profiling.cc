/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Reused from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7.
// Original: src/hccl_api_test.cc, ProfileState/StartProfiling/StopProfiling.
#include "profiling.h"
#include <cstdio>
#include <acl/acl.h>
namespace hccl_api_test {
int StartProfiling(int device_id, ProfileState& profile, const std::string& path, bool enabled)
{
    if (!enabled)
        return 0;
    aclError ret = aclprofInit(path.c_str(), path.size());
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "aclprofInit failed, ret=%d, path=%s\n", ret, path.c_str());
        return ret;
    }
    profile.initialized = true;
    const uint32_t switches = ACL_PROF_ACL_API | ACL_PROF_TASK_TIME | ACL_PROF_AICORE_METRICS | ACL_PROF_AICPU |
                              ACL_PROF_HCCL_TRACE | ACL_PROF_MSPROFTX | ACL_PROF_RUNTIME_API;
    uint32_t device = static_cast<uint32_t>(device_id);
    profile.config = aclprofCreateConfig(&device, 1, ACL_AICORE_PIPE_UTILIZATION, nullptr, switches);
    if (profile.config == nullptr) {
        std::fprintf(stderr, "aclprofCreateConfig failed.\n");
        return -1;
    }
    ret = aclprofStart(profile.config);
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "aclprofStart failed, ret=%d\n", ret);
        return ret;
    }
    profile.started = true;
    return 0;
}

int StopProfiling(ProfileState& profile)
{
    int result = 0;
    if (profile.started && profile.config != nullptr) {
        aclError ret = aclprofStop(profile.config);
        if (ret != ACL_SUCCESS)
            result = result == 0 ? ret : result;
        if (ret != ACL_SUCCESS)
            std::fprintf(stderr, "aclprofStop failed, ret=%d\n", ret);
        profile.started = false;
    }
    if (profile.config != nullptr) {
        aclError ret = aclprofDestroyConfig(profile.config);
        if (ret != ACL_SUCCESS)
            result = result == 0 ? ret : result;
        if (ret != ACL_SUCCESS)
            std::fprintf(stderr, "aclprofDestroyConfig failed, ret=%d\n", ret);
        profile.config = nullptr;
    }
    if (profile.initialized) {
        aclError ret = aclprofFinalize();
        if (ret != ACL_SUCCESS)
            result = result == 0 ? ret : result;
        if (ret != ACL_SUCCESS)
            std::fprintf(stderr, "aclprofFinalize failed, ret=%d\n", ret);
        profile.initialized = false;
    }
    return result;
}

} // namespace hccl_api_test
