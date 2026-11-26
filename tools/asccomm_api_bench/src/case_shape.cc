/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "case_shape.h"
#include "options.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace asccomm_api_bench {
namespace {
uint64_t Multiply(uint64_t a, uint64_t b)
{
    if (b != 0 && a > std::numeric_limits<uint64_t>::max() / b)
        throw std::overflow_error("case shape multiplication overflow");
    return a * b;
}
uint64_t CeilDivide(uint64_t a, uint64_t b) { return a / b + (a % b != 0 ? 1 : 0); }
uint64_t AlignUp(uint64_t count, uint64_t alignment) { return Multiply(CeilDivide(count, alignment), alignment); }
} // namespace

CaseShape MakeCaseShape(const Options& options, uint64_t requested_bytes)
{
    ValidateOptions(options);
    if (requested_bytes == 0)
        throw std::invalid_argument("requested case length must be positive");
    const uint64_t ranks = options.ranks;
    const uint64_t dtype_size = DTypeSize(options.dtype);
    uint64_t bytes = requested_bytes;
    if (options.op != Op::AllReduce) {
        bytes = std::max(bytes, ranks);
        const uint64_t alignment = bytes > 16ULL * 1024 * 1024 ? Multiply(ranks, 512) : ranks;
        bytes = bytes / alignment * alignment;
    }
    const uint64_t count = std::max<uint64_t>(1, CeilDivide(bytes, dtype_size));
    CaseShape shape;
    shape.requested_bytes = requested_bytes;
    if (options.op == Op::AllGather || options.op == Op::ReduceScatter) {
        const uint64_t per_rank = CeilDivide(count, ranks);
        const uint64_t total = Multiply(per_rank, ranks);
        shape.per_rank_count = per_rank;
        shape.input_count = options.op == Op::AllGather ? per_rank : total;
        shape.output_count = options.op == Op::AllGather ? total : per_rank;
    } else if (options.op == Op::AllToAll || options.op == Op::AllToAllV || options.op == Op::AllToAllVWrite) {
        const uint64_t large_alignment = Multiply(ranks, 128);
        const uint64_t total = AlignUp(count, count >= large_alignment ? large_alignment : ranks);
        shape.input_count = total;
        shape.output_count = total;
        shape.per_peer_count = total / ranks;
    } else {
        shape.input_count = count;
        shape.output_count = count;
    }
    const uint64_t tensor_limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (shape.input_count > tensor_limit || shape.output_count > tensor_limit) {
        throw std::overflow_error("case element count exceeds tensor int64 range");
    }
    shape.input_bytes = Multiply(shape.input_count, dtype_size);
    shape.output_bytes = Multiply(shape.output_count, dtype_size);
    if (shape.input_bytes > std::numeric_limits<size_t>::max() ||
        shape.output_bytes > std::numeric_limits<size_t>::max()) {
        throw std::overflow_error("case byte size exceeds host size_t range");
    }
    shape.bandwidth_bytes = options.op == Op::AllGather ? shape.output_bytes : shape.input_bytes;
    return shape;
}
} // namespace asccomm_api_bench
