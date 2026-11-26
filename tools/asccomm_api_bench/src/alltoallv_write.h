/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_ALLTOALLV_WRITE_H
#define ASCCOMM_API_BENCH_ALLTOALLV_WRITE_H

#include "bench_types.h"

namespace asccomm_api_bench {

// The public API writes bytes into communication windows and takes no dtype or
// usrOut. The copied legacy kernel gathers window slots into its output tensor.
// Two contiguous mission-major halves are required even for a single mission.
AllToAllVWriteLayout MakeAllToAllVWriteLayout(const Options& options, uint64_t requested_bytes);
void ValidateAllToAllVWriteLayout(const Options& options, const CaseShape& shape, const AllToAllVWriteLayout& layout);

// window_capacity_bytes must come from the actual communication window query,
// not from a tensor allocation or an assumed SDK default.
AllToAllVWritePlan MakeAllToAllVWritePlan(
    const Options& options, uint64_t requested_bytes, uint32_t global_rank, uint64_t window_capacity_bytes);
void ValidateAllToAllVWritePlan(
    const Options& options, uint64_t requested_bytes, const AllToAllVWritePlan& plan, uint64_t window_capacity_bytes);

} // namespace asccomm_api_bench
#endif
