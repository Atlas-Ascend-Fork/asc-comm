/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef MC2_BENCH_NATIVE_WRITE_ADAPTER_H
#define MC2_BENCH_NATIVE_WRITE_ADAPTER_H
#include <memory>
#include "native_device.h"
#include "legacy/hccl_api_test_common.h"
namespace asccomm_api_bench {
std::unique_ptr<hccl_api_test::HcclOpBaseTest> MakeLegacyWriteAdapter(
    const Options& options, const PlanEntry& entry, uint32_t rank, void* communicator, NativeDeviceResult& result);
} // namespace asccomm_api_bench
#endif
