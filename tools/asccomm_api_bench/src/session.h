/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_SESSION_H
#define ASCCOMM_API_BENCH_SESSION_H

#include <functional>
#include "bench_types.h"

namespace asccomm_api_bench {

// A transport failure is fatal to the enclosing MPI job. Callers must not
// attempt another collective after a transport method throws.
class Coordinator {
public:
    virtual ~Coordinator() = default;
    virtual uint32_t Rank() const = 0;
    virtual uint32_t Size() const = 0;
    virtual bool AllTrue(bool local_value) = 0;
    virtual std::string Broadcast(const std::string& root_value) = 0;
};

struct QueryPlanResult {
    std::vector<uint64_t> lengths;
    QueryMode query_mode = QueryMode::TopologyAll;
    std::vector<QueryRequest> query_requests;
    std::vector<QueryRecord> queries;
    std::vector<PlanEntry> plan;
    std::string failure_stage;
    std::string error;

    bool Ok() const { return failure_stage.empty(); }
};

using QueryPoint = std::function<QueryRecord(const QueryRequest& request)>;

// All ranks coordinate discovery and query contexts, without constructing a test plan.
QueryPlanResult QueryCandidates(const Options& options, const QueryPoint& query, Coordinator& coordinator);

// All ranks call this before measured collectives. The canonical performance
// plan is broadcast and compared on every rank after successful discovery.
// Failures preserve query diagnostics but return no plan.
QueryPlanResult QueryAndPlan(const Options& options, const QueryPoint& query, Coordinator& coordinator);

} // namespace asccomm_api_bench
#endif
