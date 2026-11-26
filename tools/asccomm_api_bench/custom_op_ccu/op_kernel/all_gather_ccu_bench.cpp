/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
#include "kernel_operator_intf.h"
#include "all_gather_ccu_bench_tiling.h"

using namespace AscendC;

namespace {
constexpr uint64_t WAIT_REPORT_MAGIC = 0xA5A55A5AA5A55A5AULL;
constexpr uint64_t NS_PER_CYCLE = 1ULL;
} // namespace

extern "C" __global__ __aicore__ void all_gather_ccu_bench(
    GM_ADDR input, GM_ADDR output, GM_ADDR workspace, GM_ADDR tiling)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIC_ONLY);
    if (GetBlockIdx() != 0 || input == nullptr || output == nullptr || workspace == nullptr || tiling == nullptr ||
        GetUserWorkspace(workspace) == nullptr) {
        return;
    }

    REGISTER_TILING_DEFAULT(AllGatherCcuBenchTilingData);
    GET_TILING_DATA_WITH_STRUCT(AllGatherCcuBenchTilingData, tiling_data, tiling);

    static constexpr HcclServerConfig HCCL_CFG = {CoreType::ON_AIC, 0};
    Hccl<HcclServerType::HCCL_SERVER_TYPE_CCU, HCCL_CFG> hccl;
    const auto context = GetHcclContext<HCCL_GROUP_ID_0>();
    if (context == nullptr) {
        return;
    }
    hccl.InitV2(context, &tiling_data);
    if (hccl.SetCcTilingV2(offsetof(AllGatherCcuBenchTilingData, mc2CcTiling)) != 0) {
        return;
    }

    const auto handle =
        hccl.AllGather<true>(input, output, tiling_data.param.count, HcclDataType(tiling_data.param.dataType), 0, 1);
    if (handle < 0) {
        return;
    }
    const int64_t waitStartCycle = GetSystemCycle();
    const auto wait_status = hccl.Wait(handle);
    const uint64_t waitNs = static_cast<uint64_t>(GetSystemCycle() - waitStartCycle) * NS_PER_CYCLE;
    if (wait_status != 0) {
        return;
    }
    hccl.Finalize();
    // Use the legacy two-word Wait report, outside the measured Wait interval.
    // Communication has completed; the next iteration restores the input data.
    GlobalTensor<uint64_t> report;
    report.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(input));
    report.SetValue(0, WAIT_REPORT_MAGIC);
    report.SetValue(1, waitNs);
    DataCacheCleanAndInvalid<uint64_t, CacheLine::SINGLE_CACHE_LINE>(report);
}
