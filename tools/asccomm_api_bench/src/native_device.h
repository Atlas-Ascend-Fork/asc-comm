/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_NATIVE_DEVICE_H
#define ASCCOMM_API_BENCH_NATIVE_DEVICE_H

#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <acl/acl.h>
#include "bench_types.h"

namespace asccomm_api_bench {

struct NativeIterationSample {
    uint32_t iteration = 0;
    bool measured = false;
    uint32_t stream_id = 0;
    uint32_t task_id = 0;
    double event_us = 0;
    bool wait_report_present = false;
    uint64_t wait_duration_ns = 0;
};

struct NativeDeviceResult {
    double event_us = 0;
    bool validated = false;
    bool write_report_present = false;
    uint64_t wait_duration_ns = 0;
    uint64_t window_capacity_bytes = 0;
    uint64_t window_verified_bytes = 0;
    // Local PLOG wall-clock bounds for this entry, outside timed iterations.
    std::string algorithm_log_begin;
    std::string algorithm_log_end;
    std::vector<NativeIterationSample> iteration_samples;
};

class NativeDeviceValidationError : public std::runtime_error {
public:
    explicit NativeDeviceValidationError(const std::string& message) : std::runtime_error(message) {}
};

// Front-end options, query planning and reporting stay outside the executor.
// The session owns only the legacy control stream/Notify/Events. The caller
// owns the HCCL communicator and main ACL stream, shared with topology queries.
void ValidateNativeDeviceOptions(const Options& options);

class NativeDeviceSession {
public:
    NativeDeviceSession(
        const Options& options, uint32_t rank, const std::string& comm_name, void* communicator, aclrtStream stream,
        bool collect_profile);
    ~NativeDeviceSession();
    NativeDeviceSession(const NativeDeviceSession&) = delete;
    NativeDeviceSession& operator=(const NativeDeviceSession&) = delete;
    NativeDeviceResult Run(const PlanEntry& entry, const CaseShape& shape, uint32_t warmup, uint32_t iterations);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace asccomm_api_bench
#endif
