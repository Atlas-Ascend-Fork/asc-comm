/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_NATIVE_PROFILE_H
#define ASCCOMM_API_BENCH_NATIVE_PROFILE_H

#include <acl/acl_prof.h>
#include "native_device.h"
#include "legacy/profiling.h"

namespace asccomm_api_bench {

// Own the copied legacy ACL collection lifecycle for one rank. The serial
// entry sequence binds legacy batch samples; ACL task IDs remain diagnostics.
class NativeProfilingSession {
public:
    NativeProfilingSession(
        const std::string& directory, uint32_t device, const Options& options, uint32_t rank, const std::string& run_id,
        const std::string& job_id, const std::string& measurement_mode = "full");
    ~NativeProfilingSession();
    NativeProfilingSession(const NativeProfilingSession&) = delete;
    NativeProfilingSession& operator=(const NativeProfilingSession&) = delete;
    void Record(const TestResult& result, const NativeDeviceResult& measured, uint32_t warmup, uint32_t iterations);
    // Stop and flush collection before publishing the submission manifest.
    void Finish();

private:
    struct Entry {
        TestResult result;
        NativeDeviceResult measured;
        uint32_t warmup = 0;
        uint32_t iterations = 0;
    };
    int Release() noexcept;
    std::string directory_, run_id_, job_id_, soc_name_, measurement_mode_;
    Options options_;
    uint32_t rank_ = 0;
    uint32_t device_ = 0;
    hccl_api_test::ProfileState profile_;
    bool finished_ = false;
    std::vector<Entry> entries_;
};

} // namespace asccomm_api_bench
#endif
