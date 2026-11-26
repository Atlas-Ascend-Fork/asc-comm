/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Copied from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7.
#ifndef MC2_BENCH_LEGACY_PROFILING_H
#define MC2_BENCH_LEGACY_PROFILING_H
#include <string>
#include <acl/acl_prof.h>
namespace hccl_api_test {
struct ProfileState {
    bool initialized = false;
    bool started = false;
    aclprofConfig* config = nullptr;
};
// The outer launcher owns paths and enablement; no process-global env mutation.
int StartProfiling(int device_id, ProfileState& profile, const std::string& path, bool enabled = true);
// Preserve the old cleanup order and propagate the first error to the report.
int StopProfiling(ProfileState& profile);
} // namespace hccl_api_test
#endif
