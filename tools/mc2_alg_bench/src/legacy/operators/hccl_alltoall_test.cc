/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Reused from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7
// Original source: opbase_test/hccl_alltoall_test.cc

#include <limits>

#include "hccl_alltoall_test.h"
#include "legacy_op_api.h"

namespace hccl_api_test {

int HcclOpBaseAlltoallTest::hccl_op_base_test() { return HcclOpBaseTest::hccl_op_base_test(); }

size_t HcclOpBaseAlltoallTest::init_malloc_Ksize_by_data()
{
    if (!AlignAllToAllElementCount(data->count, context->rank_size) ||
        data->count > std::numeric_limits<size_t>::max() / data->type_size)
        return 0;
    return static_cast<size_t>(data->count * data->type_size);
}

void HcclOpBaseAlltoallTest::init_send_recv_size_by_data(size_t& send_bytes, size_t& recv_bytes)
{
    send_bytes = malloc_kSize;
    recv_bytes = malloc_kSize;
}

bool HcclOpBaseAlltoallTest::init_case_shape()
{
    case_shape.input_count = data->count;
    case_shape.output_count = data->count;
    case_shape.per_peer_count = data->count / static_cast<uint64_t>(context->rank_size);
    case_shape.tile_m = static_cast<int64_t>(case_shape.per_peer_count);
    case_shape.input_shape = {static_cast<int64_t>(data->count), 1};
    case_shape.output_shape = case_shape.input_shape;
    return FinalizeCaseShape(*dtype_info, false, case_shape);
}

int HcclOpBaseAlltoallTest::init_op_executor(CaseResources& res)
{
    uint64_t workspace_size = 0;
    aclOpExecutor* executor = nullptr;
    int ret = MC2_LEGACY_PREPARE(AllToAll)(
        res.input.tensor, context->comm_name, context->rank_size, case_shape.tile_m,
        const_cast<char*>(options->algorithm.c_str()) MC2_LEGACY_ENGINE, res.output.tensor, &workspace_size, &executor);
    if (ret != ACL_SUCCESS)
        return ret;
    return PrepareSingleUseExecutor(*context, workspace_size, executor, res);
}

int HcclOpBaseAlltoallTest::execute_op(CaseResources& res, aclrtStream stream)
{
    if (res.executor == nullptr)
        return -1;
    aclOpExecutor* executor = res.executor;
    res.executor = nullptr;
    return MC2_LEGACY_EXECUTE(AllToAll)(res.workspace, res.workspace_size, executor, stream);
}

std::vector<uint8_t> HcclOpBaseAlltoallTest::build_expected() const
{
    return BuildSourceOrderedExpected(*context, *dtype_info, case_shape, case_shape.per_peer_count);
}

bool HcclOpBaseAlltoallTest::reports_kernel_wait_time() const { return true; }

} // namespace hccl_api_test
