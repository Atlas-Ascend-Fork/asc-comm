/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_OPTIONS_H
#define ASCCOMM_API_BENCH_OPTIONS_H

#include "bench_types.h"

namespace asccomm_api_bench {

// args contains tool arguments only, without argv[0].
Options ParseOptions(const std::vector<std::string>& args);
void ValidateOptions(const Options& options);
// Match the public hccl_api_test benchmark scripts; query-only accepts wider SDK types.
void ValidateMeasurementDType(const Options& options);
std::vector<std::string> ParseAlgorithms(const std::string& text);
// Checks only the external name boundary (prefix[...]), not library grammar
// or a mapping from registered names. Unknown/internal names remain diagnostic.
bool IsExternalAlgorithmName(const std::string& name);
// Non-integral factors use ceil(begin * factor^k) bytes; duplicate points are omitted.
// At most one million expansion steps are accepted, including duplicate raw steps.
std::vector<uint64_t> ExpandLengths(const Options& options);
// User algorithms take precedence over explicitly supplied measurement sizes.
QueryMode QueryModeForOptions(const Options& options);
std::vector<QueryRequest> MakeQueryRequests(const Options& options, const std::vector<uint64_t>& lengths);
const char* Usage();

} // namespace asccomm_api_bench
#endif
