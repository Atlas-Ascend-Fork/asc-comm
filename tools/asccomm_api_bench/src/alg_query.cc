/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "alg_query.h"

#include <algorithm>
#include <stdexcept>

#include "options.h"

namespace asccomm_api_bench {
namespace {

class CcArgsGuard {
public:
    CcArgsGuard(const QueryApi& api, QueryRecord& record) : api_(api), record_(record) {}
    ~CcArgsGuard()
    {
        if (args_ != nullptr) {
            // Native release is a C API and cannot throw. An injected callback
            // must not replace a query exception during stack unwinding.
            try {
                record_.release_ret = api_.release(args_);
            } catch (...) {
            }
        }
    }
    void** Address() { return &args_; }
    void* Get() const { return args_; }
    void Close()
    {
        if (args_ == nullptr)
            return;
        void* args = args_;
        args_ = nullptr;
        record_.release_ret = api_.release(args);
        if (record_.release_ret != 0 && record_.error_stage.empty()) {
            record_.error_stage = "release";
        }
    }
    CcArgsGuard(const CcArgsGuard&) = delete;
    CcArgsGuard& operator=(const CcArgsGuard&) = delete;

private:
    const QueryApi& api_;
    QueryRecord& record_;
    void* args_ = nullptr;
};

void ValidateInput(void* comm, const QueryInput& input, const QueryApi& api)
{
    (void)QueryModeName(input.query_mode);
    if (comm == nullptr)
        throw std::invalid_argument("query requires a communicator");
    if (!api.create || !api.release || !api.set_engine || !api.set_src_type || !api.set_dst_type ||
        !api.get_algorithms || (input.set_reduce && !api.set_reduce_type)) {
        throw std::invalid_argument("incomplete MC2 query API");
    }
    if (input.requested_bytes == 0 && input.query_mode != QueryMode::TopologyAll)
        throw std::invalid_argument("zero query bytes require topology_all mode");
    if (input.query_mode == QueryMode::TopologyAll &&
        (input.requested_bytes != 0 || !input.data_len_set || input.data_len != 0))
        throw std::invalid_argument("topology_all requires an explicit dataLen=0");
    if (input.require_data_len && (!input.data_len_set || input.data_len == 0)) {
        throw std::invalid_argument("per-length query requires a positive dataLen; zero disables length filtering");
    }
    if (input.data_len_set && !api.set_data_len) {
        throw std::invalid_argument("dataLen was supplied without its SDK range setter");
    }
    if (input.comm_engine != 2 && input.comm_engine != 6 && input.comm_engine != 5) {
        throw std::invalid_argument("unsupported query commEngine (expected 2, 6 or 5)");
    }
}

bool Failed(QueryRecord& record, uint32_t ret, const char* stage)
{
    record.ret = ret;
    if (ret == 0)
        return false;
    record.error_stage = stage;
    return true;
}

} // namespace

uint64_t QueryDataLenBytes(const Options& options, uint64_t requested_bytes)
{
    ValidateOptions(options);
    return requested_bytes;
}

QueryRecord QueryAlgorithms(void* comm, const QueryInput& input, const QueryApi& api)
{
    ValidateInput(comm, input, api);
    QueryRecord record;
    record.query_id = input.query_id;
    record.query_mode = input.query_mode;
    record.requested_bytes = input.requested_bytes;
    record.cc_type = input.cc_type;
    record.comm_engine = input.comm_engine;
    CcArgsGuard args(api, record);
    const char* stage = "create";
    try {
        if (Failed(record, api.create(args.Address()), "create")) {
            // A provider that returns a non-null allocation alongside an error must
            // still have that allocation paired with its release API.
            args.Close();
            return record;
        }
        if (args.Get() == nullptr) {
            throw std::runtime_error("Mc2GetCcArgs succeeded without returning an opaque object");
        }
        stage = "set_engine";
        if (Failed(record, api.set_engine(args.Get(), input.comm_engine), stage)) {
            args.Close();
            return record;
        }
        stage = "set_src_type";
        if (Failed(record, api.set_src_type(args.Get(), input.src_type), stage)) {
            args.Close();
            return record;
        }
        stage = "set_dst_type";
        if (Failed(record, api.set_dst_type(args.Get(), input.dst_type), stage)) {
            args.Close();
            return record;
        }
        stage = "set_reduce_type";
        if (input.set_reduce && Failed(record, api.set_reduce_type(args.Get(), input.reduce_type), stage)) {
            args.Close();
            return record;
        }
        if (input.data_len_set) {
            stage = "set_data_len";
            if (Failed(record, api.set_data_len(args.Get(), input.data_len), "set_data_len")) {
                args.Close();
                return record;
            }
            record.data_len_set = true;
            record.data_len = input.data_len;
        }

        // Each call owns its output container, so no stale entries or references to
        // another point can survive. The official query also clears on entry.
        stage = "query";
        std::vector<std::string> algorithms;
        try {
            record.ret = api.get_algorithms(comm, input.cc_type, args.Get(), algorithms);
        } catch (const QueryProviderError& error) {
            record.ret = error.Ret();
            record.error = error.what();
        }
        if (!Failed(record, record.ret, "query")) {
            if (std::find(algorithms.begin(), algorithms.end(), std::string()) != algorithms.end()) {
                throw std::runtime_error("Mc2GetSupportAlgList returned an empty algorithm name");
            }
            std::sort(algorithms.begin(), algorithms.end());
            algorithms.erase(std::unique(algorithms.begin(), algorithms.end()), algorithms.end());
            record.algorithms.swap(algorithms);
        }
        stage = "release";
        args.Close();
    } catch (const std::exception& error) {
        if (record.error_stage.empty())
            record.error_stage = std::string(stage) + "_exception";
        std::string message = error.what();
        try {
            args.Close();
        } catch (const std::exception& release_error) {
            message += std::string("; release exception: ") + release_error.what();
        } catch (...) {
            message += "; unknown release exception";
        }
        record.error = message;
        throw QueryException(message, record);
    } catch (...) {
        if (record.error_stage.empty())
            record.error_stage = std::string(stage) + "_exception";
        try {
            args.Close();
        } catch (...) {
        }
        record.error = "unknown provider exception";
        throw QueryException(record.error, record);
    }
    return record;
}

} // namespace asccomm_api_bench
