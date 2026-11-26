/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "plan.h"
#include "options.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace asccomm_api_bench {
std::vector<PlanEntry> BuildPlan(
    const Options& options, const std::vector<uint64_t>& lengths, const std::vector<QueryRecord>& queries)
{
    ValidateOptions(options);
    if (lengths != ExpandLengths(options))
        throw std::invalid_argument("plan lengths do not match request expansion");
    const QueryMode mode = QueryModeForOptions(options);
    const std::vector<QueryRequest> requests = MakeQueryRequests(options, lengths);
    if (queries.size() != requests.size())
        throw std::invalid_argument("query record count does not match selector requests");
    std::map<uint64_t, const QueryRecord*> records;
    std::set<std::string> candidates;
    for (const QueryRecord& query : queries) {
        if (query.ret != 0 || query.release_ret != 0 || !query.error_stage.empty()) {
            throw std::runtime_error(
                "query failed at request " + std::to_string(query.query_id) + ": " + query.error_stage);
        }
        if (query.query_id >= requests.size() || !records.emplace(query.query_id, &query).second)
            throw std::invalid_argument("duplicate or unexpected query id");
        const QueryRequest& expected = requests.at(query.query_id);
        if (query.query_mode != mode || query.requested_bytes != expected.requested_bytes || !query.data_len_set ||
            query.data_len != expected.data_len || query.comm_engine != CommEngineCode(options.engine))
            throw std::invalid_argument("query parameters do not match selector request");
        if (query.cc_type != queries.front().cc_type)
            throw std::invalid_argument("query collective type differs across requests");
        for (const std::string& name : query.algorithms) {
            if (!IsExternalAlgorithmName(name)) {
                throw std::invalid_argument("query name contract is unresolved: expected an external [] name; retain "
                                            "the raw name for diagnosis");
            }
            candidates.insert(name);
        }
    }
    const std::set<std::string> requested(options.algorithms.begin(), options.algorithms.end());
    const std::set<std::string>& selected = requested.empty() ? candidates : requested;
    std::vector<PlanEntry> plan;
    for (uint64_t length : lengths) {
        if (selected.empty()) {
            PlanEntry entry;
            entry.id = plan.size();
            entry.requested_bytes = length;
            entry.kind = PlanKind::Unsupported;
            plan.push_back(entry);
        }
        for (const std::string& algorithm : selected) {
            PlanEntry entry;
            entry.id = plan.size();
            entry.requested_bytes = length;
            entry.algorithm = algorithm;
            entry.kind = candidates.count(algorithm) != 0 ? PlanKind::Supported : PlanKind::Unsupported;
            plan.push_back(entry);
        }
    }
    return plan;
}
} // namespace asccomm_api_bench
