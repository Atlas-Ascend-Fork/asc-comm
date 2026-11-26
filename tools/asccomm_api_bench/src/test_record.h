/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_TEST_RECORD_H
#define ASCCOMM_API_BENCH_TEST_RECORD_H

#include <iosfwd>
#include <map>
#include <string>
#include "bench_types.h"

namespace asccomm_api_bench {

// The host monotonic clock alone cannot correlate device profiling samples.
// The profiling integration must establish that correspondence separately.
struct PhaseRecord {
    std::string phase;
    std::string event;
    int rank = 0;
    uint64_t process = 0;
    uint64_t entry_id = 0;
    uint32_t batch = 0;
    int64_t iteration = -1;
    uint64_t host_monotonic_ns = 0;
};

const char* PlanKindName(PlanKind kind);
const char* ResultStatusName(ResultStatus status);

// One writer per stream; the caller serializes writes. Emit one job header and
// one final, explicitly selected result per plan entry. Retried phase records
// retain their batch identifiers; metrics from different batches are not merged.
class RecordWriter {
public:
    RecordWriter(std::ostream& out, const std::string& job_id);
    void WriteJob(
        const Options& options, const std::vector<uint64_t>& lengths,
        const std::map<std::string, std::string>& metadata = {});
    void WriteQuery(const QueryRecord& query);
    void WritePlan(const PlanEntry& entry);
    void WriteResult(const TestResult& result);
    void WritePhase(const PhaseRecord& phase);
    void WriteFailure(const std::string& stage, int64_t ret, const std::string& error);

private:
    void Write(const std::string& type, const std::string& fields);
    std::ostream& out_;
    std::string job_id_;
    bool job_written_ = false;
    Options options_;
};

} // namespace asccomm_api_bench
#endif
