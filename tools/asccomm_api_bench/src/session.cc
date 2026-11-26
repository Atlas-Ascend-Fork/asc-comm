/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "session.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

#include "alg_query.h"
#include "options.h"
#include "plan.h"

namespace asccomm_api_bench {
namespace {

void Field(std::ostream& out, const std::string& value) { out << value.size() << ':' << value; }

std::string Names(std::vector<std::string> names)
{
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << names.size() << ':';
    for (const auto& name : names)
        Field(out, name);
    return out.str();
}

std::string QueryContext(const QueryRecord& record)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << record.query_id << ':' << static_cast<unsigned int>(record.query_mode) << ':' << record.requested_bytes
        << ':' << static_cast<unsigned int>(record.cc_type) << ':' << static_cast<unsigned int>(record.comm_engine)
        << ':' << record.data_len_set << ':';
    if (record.data_len_set)
        out << record.data_len << ':';
    Field(out, Names(record.algorithms));
    return out.str();
}

std::string PlanContext(const std::vector<PlanEntry>& plan)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << plan.size() << ':';
    for (const auto& entry : plan) {
        out << entry.id << ':' << entry.requested_bytes << ':' << static_cast<unsigned int>(entry.kind) << ':';
        Field(out, entry.algorithm);
    }
    return out.str();
}

std::string Scenario(
    const Options& options, const std::vector<uint64_t>& lengths, const std::vector<QueryRequest>& requests)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    Field(out, OpName(options.op));
    Field(out, EngineName(options.engine));
    Field(out, options.dtype);
    Field(out, options.reduce);
    out << options.ranks << ':' << options.devices_per_node << ':';
    out << options.begin << ':' << options.end << ':' << options.begin_set << ':' << options.end_set << ':';
    out << std::setprecision(17) << options.factor << ':';
    out << options.factor_set << ':';
    out << options.increment_set << ':' << options.increment << ':';
    Field(out, Names(options.algorithms));
    out << lengths.size() << ':';
    for (uint64_t length : lengths)
        out << length << ':';
    Field(out, QueryModeName(QueryModeForOptions(options)));
    out << requests.size() << ':';
    for (const auto& request : requests)
        out << request.id << ':' << request.requested_bytes << ':' << request.data_len << ':';
    return out.str();
}

QueryPlanResult Fail(QueryPlanResult result, const char* stage, const std::string& error)
{
    result.plan.clear();
    result.failure_stage = stage;
    result.error = error;
    return result;
}

} // namespace

static QueryPlanResult RunQuerySession(
    const Options& options, const QueryPoint& query, Coordinator& coordinator, bool build_plan)
{
    QueryPlanResult result;
    std::string local_error;
    try {
        ValidateOptions(options);
        if (options.ranks != coordinator.Size()) {
            throw std::invalid_argument("-n does not match MPI world size");
        }
        if (!query)
            throw std::invalid_argument("query callback is unavailable");
        result.lengths = ExpandLengths(options);
        result.query_mode = QueryModeForOptions(options);
        result.query_requests = MakeQueryRequests(options, result.lengths);
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    if (!coordinator.AllTrue(local_error.empty())) {
        return Fail(result, "parameters", local_error.empty() ? "another rank rejected the parameters" : local_error);
    }

    const std::string scenario =
        Scenario(options, result.lengths, result.query_requests) + (build_plan ? ":benchmark" : ":query_only");
    const std::string root_scenario = coordinator.Broadcast(scenario);
    if (!coordinator.AllTrue(scenario == root_scenario)) {
        return Fail(result, "scenario_agreement", "rank input scenarios differ");
    }

    for (const QueryRequest& request : result.query_requests) {
        QueryRecord record;
        record.query_id = request.id;
        record.query_mode = request.query_mode;
        record.requested_bytes = request.requested_bytes;
        record.comm_engine = CommEngineCode(options.engine);
        local_error.clear();
        try {
            record = query(request);
            if (record.query_id != request.id || record.query_mode != request.query_mode ||
                record.requested_bytes != request.requested_bytes ||
                record.comm_engine != CommEngineCode(options.engine)) {
                local_error = "query record does not match the requested point/engine";
            } else if (record.ret != 0 || record.release_ret != 0 || !record.error_stage.empty()) {
                local_error = "query failed at " + record.error_stage;
                if (!record.error.empty())
                    local_error += ": " + record.error;
            } else if (!record.data_len_set || record.data_len != request.data_len) {
                local_error = "query record does not preserve the requested dataLen";
            }
        } catch (const QueryException& error) {
            record = error.Record();
            local_error = error.what();
        } catch (const std::exception& error) {
            record.error_stage = "query_exception";
            local_error = error.what();
        }
        result.queries.push_back(record);
        if (!coordinator.AllTrue(local_error.empty())) {
            return Fail(result, "query", local_error.empty() ? "query failed on another rank" : local_error);
        }

        const std::string context = QueryContext(record);
        const std::string root_context = coordinator.Broadcast(context);
        if (!coordinator.AllTrue(context == root_context)) {
            return Fail(result, "query_agreement", "rank query parameters or algorithm sets differ");
        }
    }

    if (!build_plan)
        return result;
    local_error.clear();
    try {
        result.plan = BuildPlan(options, result.lengths, result.queries);
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    if (!coordinator.AllTrue(local_error.empty())) {
        return Fail(result, "plan", local_error.empty() ? "plan creation failed on another rank" : local_error);
    }
    const std::string local_plan = PlanContext(result.plan);
    const std::string root_plan = coordinator.Broadcast(local_plan);
    if (!coordinator.AllTrue(local_plan == root_plan)) {
        return Fail(result, "plan_agreement", "local plan differs from the coordinator's plan");
    }
    return result;
}

QueryPlanResult QueryCandidates(const Options& options, const QueryPoint& query, Coordinator& coordinator)
{
    return RunQuerySession(options, query, coordinator, false);
}

QueryPlanResult QueryAndPlan(const Options& options, const QueryPoint& query, Coordinator& coordinator)
{
    return RunQuerySession(options, query, coordinator, true);
}

} // namespace asccomm_api_bench
