/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_PLAN_H
#define ASCCOMM_API_BENCH_PLAN_H
#include "bench_types.h"
namespace asccomm_api_bench {
// Match actual selector requests by query id, union their candidates, and test
// the union across every measured length. Explicit names outside the topology
// candidate set remain unsupported diagnostics and must never be executed.
// Failure, duplicate/missing requests or inconsistent context abort planning.
std::vector<PlanEntry> BuildPlan(
    const Options& options, const std::vector<uint64_t>& lengths, const std::vector<QueryRecord>& queries);
} // namespace asccomm_api_bench
#endif
