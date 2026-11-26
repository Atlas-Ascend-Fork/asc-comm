/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_NATIVE_QUERY_H
#define ASCCOMM_API_BENCH_NATIVE_QUERY_H

#include "alg_query.h"

namespace asccomm_api_bench {

// Requires a formal SDK declaring the C ABI JSON Mc2GetSupportAlgList and
// Mc2SetCcDataLenRange. This
// source is intentionally excluded from portable builds.
QueryApi MakeNativeQueryApi();
// Binds the exact selector request, including an explicit setter call with zero.
QueryInput MakeNativeQueryInput(const Options& options, const QueryRequest& request);
// Convenience using the policy selected by options. Topology mode requires
// requested_bytes=0; positive bytes in that mode are rejected rather than
// silently bypassing algorithm-name precedence.
QueryInput MakeNativeQueryInput(const Options& options, uint64_t requested_bytes);

} // namespace asccomm_api_bench
#endif
