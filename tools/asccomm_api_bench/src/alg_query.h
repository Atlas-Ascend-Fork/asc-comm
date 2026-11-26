/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_ALG_QUERY_H
#define ASCCOMM_API_BENCH_ALG_QUERY_H

#include <functional>
#include <stdexcept>

#include "bench_types.h"

namespace asccomm_api_bench {

// Mc2SetCcDataLenRange supplies per-rank bytes (count * source dtype size). Patterns
// may compare these bytes, total bytes or element counts derived from src type.
// Zero skips length clauses; it does not request a zero-byte data point.
// QueryApi remains injectable for portable tests; native code binds the public
// range setter with [bytes, bytes] for each point, or [0, 0] for topology queries.
using DataLenSetter = std::function<uint32_t(void*, uint64_t)>;

// A provider can attach SDK diagnostics while preserving its numeric failure code.
class QueryProviderError : public std::runtime_error {
public:
    QueryProviderError(uint32_t ret, const std::string& message) : std::runtime_error(message), ret_(ret)
    {
        if (ret == 0)
            throw std::invalid_argument("a provider failure requires a nonzero return code");
    }
    uint32_t Ret() const { return ret_; }

private:
    uint32_t ret_;
};

struct QueryApi {
    std::function<uint32_t(void**)> create;
    std::function<uint32_t(void*)> release;
    std::function<uint32_t(void*, uint8_t)> set_engine;
    std::function<uint32_t(void*, uint8_t)> set_src_type;
    std::function<uint32_t(void*, uint8_t)> set_dst_type;
    std::function<uint32_t(void*, uint8_t)> set_reduce_type;
    DataLenSetter set_data_len;
    // Internal decoded names; the native adapter reads the public C ABI JSON buffer.
    std::function<uint32_t(void*, uint8_t, void*, std::vector<std::string>&)> get_algorithms;
};

struct QueryInput {
    uint64_t query_id = 0;
    QueryMode query_mode = QueryMode::SinglePoint;
    uint64_t requested_bytes = 0;
    uint8_t cc_type = 0;
    uint8_t comm_engine = 0;
    uint8_t src_type = 0;
    uint8_t dst_type = 0;
    uint8_t reduce_type = 0;
    bool set_reduce = false;
    bool data_len_set = false;
    uint64_t data_len = 0;
    bool require_data_len = false;
};

// The selector receives the raw requested byte count. Shape alignment and
// operation-specific bandwidth arithmetic apply only to actual execution.
uint64_t QueryDataLenBytes(const Options& options, uint64_t requested_bytes);

// Carries partial API diagnostics when an injected/provider callback throws.
// The allocated argument has already been released before this is propagated.
class QueryException : public std::runtime_error {
public:
    QueryException(const std::string& message, const QueryRecord& record) : std::runtime_error(message), record_(record)
    {}
    const QueryRecord& Record() const { return record_; }

private:
    QueryRecord record_;
};

// HCCL_SUCCESS is zero. API failures are returned verbatim in QueryRecord.
// Invalid wiring/input throws invalid_argument. Callback exceptions propagate
// after the allocated opaque argument has been released.
QueryRecord QueryAlgorithms(void* comm, const QueryInput& input, const QueryApi& api);

} // namespace asccomm_api_bench
#endif
