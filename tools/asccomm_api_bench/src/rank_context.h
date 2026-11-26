/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_RANK_CONTEXT_H
#define ASCCOMM_API_BENCH_RANK_CONTEXT_H

#include "bench_types.h"

namespace asccomm_api_bench {

// local_rank/local_size must come from the actual node-local MPI communicator.
// The logical device is an ACL-visible index, not a physical PROF device_N ID.
struct RankContext {
    uint32_t global_rank = 0;
    uint32_t world_size = 0;
    uint32_t local_rank = 0;
    uint32_t local_size = 0;
    uint32_t node_index = 0;
    uint32_t node_count = 0;
    uint32_t device_id = 0;
    std::string hostname;
};

// Current launcher contract: equally sized nodes and contiguous global ranks.
// Device count/visibility is checked separately, after the runtime is available.
void ValidateRankContext(const Options& options, const RankContext& context);

// Empty text means the default local-rank mapping. An explicit list is kept in
// user order; duplicate, malformed and unavailable visible indices are rejected.
std::vector<uint32_t> ParseDeviceList(const std::string& text, uint32_t available_devices);
uint32_t SelectLocalDevice(
    uint32_t local_rank, uint32_t local_size, uint32_t available_devices, const std::vector<uint32_t>& devices = {});

} // namespace asccomm_api_bench
#endif
