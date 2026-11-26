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
#include "hccl_api_test_measurement.h"
#include "all_to_all_custom_v3_tiling.h"

using namespace AscendC;

extern "C" __global__ __aicore__ void all_to_all_custom_v3(
    GM_ADDR input, GM_ADDR output, GM_ADDR workspace, GM_ADDR tiling)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIC_ONLY);
    if (GetBlockIdx() != 0 || workspace == nullptr || tiling == nullptr || GetUserWorkspace(workspace) == nullptr) {
        return;
    }

    REGISTER_TILING_DEFAULT(AllToAllCustomV3TilingData);
    GET_TILING_DATA_WITH_STRUCT(AllToAllCustomV3TilingData, tiling_data, tiling);

    static constexpr HcclServerConfig HCCL_CFG = {CoreType::ON_AIC, 0};
    Hccl<HcclServerType::HCCL_SERVER_TYPE_AICPU, HCCL_CFG> hccl;
    hccl.InitV2(GetHcclContext<HCCL_GROUP_ID_0>(), &tiling_data);
    hccl.SetCcTilingV2(offsetof(AllToAllCustomV3TilingData, mc2CcTiling));

    const auto handle = hccl.AlltoAll<HCCL_API_TEST_AUTO_COMMIT>(
        input, output, tiling_data.param.peerCount, HcclDataType(tiling_data.param.dataType), 0, 1);
    HcclApiTestCommit(hccl, handle);
    hccl.Wait(handle);
    hccl.Finalize();
}
