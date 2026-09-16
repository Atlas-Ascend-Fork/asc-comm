/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_CCU_RESOURCE_LOCAL_H
#define ASC_CCU_RESOURCE_LOCAL_H

#include <array>
#include <cstdint>
#include <vector>

#include "hcomm/hcomm_ccu_resource.h"

namespace asc {

/** asc 本地 range 记录：wire POD（HcommCcuResRange 只有 startId/count）加上归属的类型与 die */
struct asc_ccu_res_range {
    uint32_t resource_type{0}; // HcommCcuBatchResType
    uint32_t die_id{0};
    uint32_t start_id{0};
    uint32_t count{0};
};

using asc_ccu_res_ranges = std::array<std::vector<asc_ccu_res_range>, HCOMM_CCU_MAX_DIE_NUM>;

struct asc_ccu_res_request {
    uint32_t count[HCOMM_CCU_BATCH_RES_TYPE_COUNT][HCOMM_CCU_MAX_DIE_NUM]{};
};

struct asc_ccu_res_repository {
    asc_ccu_res_ranges loop_engine{};
    asc_ccu_res_ranges block_loop_engine{};
    asc_ccu_res_ranges ms{};
    asc_ccu_res_ranges block_ms{};
    asc_ccu_res_ranges cke{};
    asc_ccu_res_ranges block_cke{};
    asc_ccu_res_ranges xn{};
    asc_ccu_res_ranges block_xn{};
    asc_ccu_res_ranges gsa{};
    asc_ccu_res_ranges block_gsa{};
    asc_ccu_res_ranges mission{};
    // INS 不再入本地资源池：指令空间经 ascCustom.allocInstSpace 向 hcomm 按需申请，随 RegisterContext 归还
};

} // namespace asc

#endif // ASC_CCU_RESOURCE_LOCAL_H
