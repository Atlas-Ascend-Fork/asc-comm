/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "mock_query.h"

#include <stdexcept>

#include "alg_query.h"
#include "options.h"

namespace asccomm_api_bench {
namespace {

const uint32_t MOCK_QUERY_ERROR = 9001;

struct MockArgs {
    uint8_t engine = 0;
    uint8_t source_type = 0;
    uint8_t destination_type = 0;
    uint8_t reduction = 0;
    bool length_set = false;
    uint64_t data_len = 0;
};

struct MockContext {
    Op op;
    MockQueryScenario scenario;
    uint32_t rank;
    uint8_t engine;
};

uint8_t MockCollectiveCode(Op op)
{
    switch (op) {
        case Op::AllGather:
            return 0x80;
        case Op::AllReduce:
            return 0x81;
        case Op::ReduceScatter:
            return 0x82;
        case Op::AllToAll:
            return 0x83;
        case Op::AllToAllV:
            return 0x84;
        case Op::AllToAllVWrite:
            return 0x85;
    }
    throw std::invalid_argument("invalid mock collective");
}

void ValidateRequest(const Options& options, const QueryRequest& request, uint32_t rank)
{
    ValidateOptions(options);
    if (rank >= options.ranks)
        throw std::invalid_argument("mock query rank is outside the job");
    const QueryMode mode = QueryModeForOptions(options);
    if (request.query_mode != mode || request.data_len != request.requested_bytes)
        throw std::invalid_argument("mock query request does not match the selected query policy");
    if (mode == QueryMode::TopologyAll) {
        if (request.id != 0 || request.data_len != 0)
            throw std::invalid_argument("mock topology query requires request zero and explicit dataLen=0");
        return;
    }
    const auto lengths = ExpandLengths(options);
    if (request.id >= lengths.size() || request.data_len != lengths.at(request.id))
        throw std::invalid_argument("mock query request is not an expanded measurement point");
}

std::vector<std::string> Candidates(const MockContext& context, uint64_t bytes)
{
    const bool write = context.op == Op::AllToAllVWrite;
    switch (context.scenario) {
        case MockQueryScenario::Basic:
            return write ? std::vector<std::string>{"sole[mesh]"} : std::vector<std::string>{"sole[mesh]", "sole[nhr]"};
        case MockQueryScenario::BySize:
            if (bytes == 0)
                return write ? std::vector<std::string>{"sole[mesh]"} :
                               std::vector<std::string>{"sole[mesh]", "sole[nhr]"};
            if (bytes <= 32ULL * 1024)
                return {"sole[mesh]"};
            if (bytes <= 64ULL * 1024 && !write)
                return {"sole[nhr]"};
            return {};
        case MockQueryScenario::Empty:
        case MockQueryScenario::QueryError:
            return {};
        case MockQueryScenario::RankMismatch:
            if (context.rank == 0)
                return {"sole[mesh]"};
            return write ? std::vector<std::string>{} : std::vector<std::string>{"sole[nhr]"};
    }
    throw std::invalid_argument("invalid mock query scenario");
}

} // namespace

MockQueryScenario ParseMockQueryScenario(const std::string& name)
{
    if (name == "basic")
        return MockQueryScenario::Basic;
    if (name == "by-size")
        return MockQueryScenario::BySize;
    if (name == "empty")
        return MockQueryScenario::Empty;
    if (name == "query-error")
        return MockQueryScenario::QueryError;
    if (name == "rank-mismatch")
        return MockQueryScenario::RankMismatch;
    throw std::invalid_argument("unknown mock query scenario: " + name);
}

const char* MockQueryScenarioName(MockQueryScenario scenario)
{
    switch (scenario) {
        case MockQueryScenario::Basic:
            return "basic";
        case MockQueryScenario::BySize:
            return "by-size";
        case MockQueryScenario::Empty:
            return "empty";
        case MockQueryScenario::QueryError:
            return "query-error";
        case MockQueryScenario::RankMismatch:
            return "rank-mismatch";
    }
    throw std::invalid_argument("invalid mock query scenario");
}

QueryRecord QueryMockAlgorithms(
    const Options& options, const QueryRequest& request, MockQueryScenario scenario, uint32_t rank)
{
    (void)MockQueryScenarioName(scenario);
    ValidateRequest(options, request, rank);
    MockContext context = {options.op, scenario, rank, CommEngineCode(options.engine)};
    QueryApi api;
    api.create = [](void** output) {
        *output = new MockArgs();
        return 0U;
    };
    api.release = [](void* args) {
        delete static_cast<MockArgs*>(args);
        return 0U;
    };
    api.set_engine = [](void* args, uint8_t value) {
        static_cast<MockArgs*>(args)->engine = value;
        return 0U;
    };
    api.set_src_type = [](void* args, uint8_t value) {
        static_cast<MockArgs*>(args)->source_type = value;
        return 0U;
    };
    api.set_dst_type = [](void* args, uint8_t value) {
        static_cast<MockArgs*>(args)->destination_type = value;
        return 0U;
    };
    api.set_reduce_type = [](void* args, uint8_t value) {
        static_cast<MockArgs*>(args)->reduction = value;
        return 0U;
    };
    api.set_data_len = [](void* args, uint64_t value) {
        auto& mock = *static_cast<MockArgs*>(args);
        mock.length_set = true;
        mock.data_len = value;
        return 0U;
    };
    api.get_algorithms = [](void* comm, uint8_t cc_type, void* args, std::vector<std::string>& names) {
        const auto& context = *static_cast<MockContext*>(comm);
        const auto& mock = *static_cast<MockArgs*>(args);
        // These are deliberately local synthetic tags, not SDK enum mappings.
        if (cc_type != MockCollectiveCode(context.op) || mock.engine != context.engine || !mock.length_set ||
            mock.source_type != 0xF0 || mock.destination_type != 0xF0 ||
            ((context.op == Op::AllReduce || context.op == Op::ReduceScatter) && mock.reduction != 0xF1))
            throw std::runtime_error("incomplete mock query parameters");
        if (context.scenario == MockQueryScenario::QueryError)
            return MOCK_QUERY_ERROR;
        names = Candidates(context, mock.data_len);
        return 0U;
    };
    QueryInput input;
    input.query_id = request.id;
    input.query_mode = request.query_mode;
    input.requested_bytes = request.requested_bytes;
    input.cc_type = MockCollectiveCode(options.op);
    input.comm_engine = CommEngineCode(options.engine);
    input.src_type = 0xF0;
    input.dst_type = 0xF0;
    input.set_reduce = options.op == Op::AllReduce || options.op == Op::ReduceScatter;
    input.reduce_type = 0xF1;
    input.data_len_set = true;
    input.require_data_len = request.query_mode != QueryMode::TopologyAll;
    input.data_len = request.data_len;
    return QueryAlgorithms(&context, input, api);
}

} // namespace asccomm_api_bench
