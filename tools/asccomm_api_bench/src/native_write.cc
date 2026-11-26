/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "native_write_adapter.h"

#include <hccl/hccl_res.h>
#include "alltoallv_write.h"
#include "legacy/operators/hccl_alltoallv_write_test.h"

namespace asccomm_api_bench {
std::unique_ptr<hccl_api_test::HcclOpBaseTest> MakeLegacyWriteAdapter(
    const Options& options, const PlanEntry& entry, uint32_t rank, void* comm, NativeDeviceResult& result)
{
    void* window = nullptr;
    uint64_t capacity = 0;
    const int ret = HcclGetHcclBuffer(static_cast<HcclComm>(comm), &window, &capacity);
    if (ret != 0 || window == nullptr)
        throw std::runtime_error("HcclGetHcclBuffer failed, ret=" + std::to_string(ret));
    const auto plan = MakeAllToAllVWritePlan(options, entry.requested_bytes, rank, capacity);
    ValidateAllToAllVWritePlan(options, entry.requested_bytes, plan, capacity);
    result.window_capacity_bytes = capacity;
    return std::unique_ptr<hccl_api_test::HcclOpBaseTest>(
        new hccl_api_test::HcclOpBaseAlltoallvWriteTest(reinterpret_cast<uintptr_t>(window)));
}
} // namespace asccomm_api_bench
