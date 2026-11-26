/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_MOCK_QUERY_H
#define ASCCOMM_API_BENCH_MOCK_QUERY_H

#include "bench_types.h"

namespace asccomm_api_bench {

enum class MockQueryScenario { Basic, BySize, Empty, QueryError, RankMismatch };

MockQueryScenario ParseMockQueryScenario(const std::string& name);
const char* MockQueryScenarioName(MockQueryScenario scenario);

// Replaces only algorithm discovery. Candidates are predetermined diagnostics,
// not a claim of SDK/topology support or proof of the algorithm actually run.
// cc_type uses tool-local 0x80..0x85 codes, never formal SDK collective enums.
// The caller must mark these records with query_source=mock in job metadata.
// No device execution, timings, correctness results, or hit evidence is created.
QueryRecord QueryMockAlgorithms(
    const Options& options, const QueryRequest& request, MockQueryScenario scenario, uint32_t rank);

} // namespace asccomm_api_bench
#endif
