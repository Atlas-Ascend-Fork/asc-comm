/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "alltoallv_write.h"

#include <stdexcept>
#include <limits>

#include "case_shape.h"
#include "options.h"

namespace asccomm_api_bench {
namespace {
bool SameShape(const CaseShape& a, const CaseShape& b)
{
    return a.requested_bytes == b.requested_bytes && a.input_count == b.input_count &&
           a.output_count == b.output_count && a.input_bytes == b.input_bytes && a.output_bytes == b.output_bytes &&
           a.bandwidth_bytes == b.bandwidth_bytes && a.per_rank_count == b.per_rank_count &&
           a.per_peer_count == b.per_peer_count;
}
} // namespace

AllToAllVWriteLayout MakeAllToAllVWriteLayout(const Options& options, uint64_t requested_bytes)
{
    if (options.op != Op::AllToAllVWrite)
        throw std::invalid_argument("write layout requires alltoallvwrite");
    const CaseShape shape = MakeCaseShape(options, requested_bytes);
    AllToAllVWriteLayout layout;
    layout.enabled = true;
    layout.wire_dtype = options.dtype;
    layout.per_peer_bytes = shape.input_bytes / options.ranks;
    layout.local_data_size_bytes = layout.per_peer_bytes;
    if (layout.per_peer_bytes > std::numeric_limits<uint64_t>::max() - 511)
        throw std::overflow_error("write slot stride overflow");
    const uint64_t stride = (layout.per_peer_bytes + 511) / 512 * 512;
    if (stride > std::numeric_limits<uint64_t>::max() / options.ranks)
        throw std::overflow_error("write window size overflow");
    layout.window_bytes = stride * options.ranks;
    const uint64_t first = layout.per_peer_bytes / 2;
    // Shape construction has already checked ranks * peer bytes and host limits.
    for (uint64_t rank = 0; rank < options.ranks; ++rank) {
        const uint64_t offset = rank * layout.per_peer_bytes;
        layout.send_offsets_bytes.push_back(offset);
        layout.send_sizes_bytes.push_back(first);
        layout.remote_window_offsets_bytes.push_back(rank * stride);
    }
    for (uint64_t rank = 0; rank < options.ranks; ++rank) {
        layout.send_offsets_bytes.push_back(rank * layout.per_peer_bytes + first);
        layout.send_sizes_bytes.push_back(layout.per_peer_bytes - first);
    }
    return layout;
}

void ValidateAllToAllVWriteLayout(const Options& options, const CaseShape& shape, const AllToAllVWriteLayout& layout)
{
    const CaseShape expected_shape = MakeCaseShape(options, shape.requested_bytes);
    const AllToAllVWriteLayout expected = MakeAllToAllVWriteLayout(options, shape.requested_bytes);
    if (!SameShape(shape, expected_shape) || !layout.enabled || layout.wire_dtype != expected.wire_dtype ||
        layout.per_peer_bytes != expected.per_peer_bytes || layout.send_offsets_bytes != expected.send_offsets_bytes ||
        layout.send_sizes_bytes != expected.send_sizes_bytes ||
        layout.remote_window_offsets_bytes != expected.remote_window_offsets_bytes ||
        layout.local_data_size_bytes != expected.local_data_size_bytes ||
        layout.window_bytes != expected.window_bytes) {
        throw std::invalid_argument("write layout/shape differs from the balanced byte/window contract");
    }
}

AllToAllVWritePlan MakeAllToAllVWritePlan(
    const Options& options, uint64_t requested_bytes, uint32_t global_rank, uint64_t window_capacity_bytes)
{
    if (global_rank >= options.ranks)
        throw std::invalid_argument("write plan global rank is outside the communicator");
    const AllToAllVWriteLayout layout = MakeAllToAllVWriteLayout(options, requested_bytes);
    if (window_capacity_bytes < layout.window_bytes)
        throw std::invalid_argument("actual communication window is too small for the write plan");
    AllToAllVWritePlan plan;
    plan.global_rank = global_rank;
    plan.send_offsets_bytes = layout.send_offsets_bytes;
    plan.send_sizes_bytes = layout.send_sizes_bytes;
    plan.remote_window_offset_bytes = layout.remote_window_offsets_bytes[global_rank];
    plan.local_data_size_bytes = layout.local_data_size_bytes;
    plan.window_bytes = layout.window_bytes;
    return plan;
}

void ValidateAllToAllVWritePlan(
    const Options& options, uint64_t requested_bytes, const AllToAllVWritePlan& plan, uint64_t window_capacity_bytes)
{
    const AllToAllVWritePlan expected =
        MakeAllToAllVWritePlan(options, requested_bytes, plan.global_rank, window_capacity_bytes);
    if (plan.send_offsets_bytes != expected.send_offsets_bytes || plan.send_sizes_bytes != expected.send_sizes_bytes ||
        plan.remote_window_offset_bytes != expected.remote_window_offset_bytes ||
        plan.local_data_size_bytes != expected.local_data_size_bytes || plan.window_bytes != expected.window_bytes) {
        throw std::invalid_argument("write plan does not match this rank's byte/window layout");
    }
}
} // namespace asccomm_api_bench
