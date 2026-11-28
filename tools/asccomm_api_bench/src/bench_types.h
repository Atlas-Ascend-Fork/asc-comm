/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_TYPES_H
#define ASCCOMM_API_BENCH_TYPES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace asccomm_api_bench {

enum class Op { AllGather, AllReduce, ReduceScatter, AllToAll, AllToAllV, AllToAllVWrite };
enum class Engine { AicpuTs, CcuSched, CcuMs };

struct Options {
    Op op = Op::AllReduce;
    bool op_set = false;
    Engine engine = Engine::AicpuTs;
    uint32_t ranks = 8;
    uint32_t devices_per_node = 8;
    // Match Mc2GetCcArgs defaults for both query and execution.
    std::string dtype = "fp16";
    std::string reduce = "sum";
    uint64_t begin = 32768;
    uint64_t end = 268435456;
    bool begin_set = false;
    bool end_set = false;
    double factor = 2;
    uint64_t increment = 0;
    bool factor_set = false;
    bool increment_set = false;
    std::vector<std::string> algorithms;
};

struct CaseShape {
    uint64_t requested_bytes = 0;
    uint64_t input_count = 0;
    uint64_t output_count = 0;
    uint64_t input_bytes = 0;
    uint64_t output_bytes = 0;
    uint64_t bandwidth_bytes = 0;
    uint64_t per_rank_count = 0;
    uint64_t per_peer_count = 0;
};

// Legacy Write payload/output tensors are contiguous. Receive slots in the SDK
// window have 512-byte strides and are copied into output by the device kernel.
struct AllToAllVWriteLayout {
    bool enabled = false;
    std::string wire_dtype;
    uint64_t per_peer_bytes = 0;
    std::vector<uint64_t> send_offsets_bytes;
    std::vector<uint64_t> send_sizes_bytes;
    std::vector<uint64_t> remote_window_offsets_bytes;
    uint64_t local_data_size_bytes = 0;
    uint64_t window_bytes = 0;
};

struct AllToAllVWritePlan {
    uint32_t global_rank = 0;
    std::vector<uint64_t> send_offsets_bytes;
    std::vector<uint64_t> send_sizes_bytes;
    uint64_t remote_window_offset_bytes = 0;
    uint64_t local_data_size_bytes = 0;
    uint64_t window_bytes = 0;
};

enum class QueryMode { TopologyAll, SinglePoint, RangeUnion };

// A selector request is independent from the measured lengths. Zero means
// topology-only selection and is never passed to MakeCaseShape.
struct QueryRequest {
    uint64_t id = 0;
    uint64_t requested_bytes = 0;
    uint64_t data_len = 0;
    QueryMode query_mode = QueryMode::TopologyAll;
};

struct QueryRecord {
    uint64_t query_id = 0;
    QueryMode query_mode = QueryMode::SinglePoint;
    uint64_t requested_bytes = 0;
    uint8_t cc_type = 0;
    uint8_t comm_engine = 0;
    bool data_len_set = false;
    uint64_t data_len = 0;
    uint32_t ret = 0;
    uint32_t release_ret = 0;
    std::string error_stage;
    std::vector<std::string> algorithms;
    std::string error;
};

enum class PlanKind { Supported, FallbackAttempt, Unsupported };
struct PlanEntry {
    uint64_t id = 0;
    uint64_t requested_bytes = 0;
    std::string algorithm;
    PlanKind kind = PlanKind::Supported;
};

enum class ResultStatus {
    Pending,
    Hit,
    Fallback,
    AlgorithmUnconfirmed,
    Unsupported,
    ExecutionFailed,
    ValidationFailed,
    CollectionFailed,
    ParseFailed,
    Unfinished
};
struct Metric {
    std::string name;
    double value = 0;
};
struct TestResult {
    PlanEntry entry;
    CaseShape shape;
    ResultStatus status = ResultStatus::Pending;
    std::string actual_algorithm;
    std::string evidence;
    std::string failure_stage;
    std::string error;
    int64_t ret = 0;
    uint32_t batch = 0;
    std::vector<Metric> metrics;
    AllToAllVWriteLayout write_layout;
};

const char* QueryModeName(QueryMode mode);
const char* OpName(Op op);
const char* EngineName(Engine engine);
// MC2 query/tiling commEngine values; HCCL communicator config is mapped separately.
uint8_t CommEngineCode(Engine engine);
size_t DTypeSize(const std::string& dtype);

} // namespace asccomm_api_bench
#endif
