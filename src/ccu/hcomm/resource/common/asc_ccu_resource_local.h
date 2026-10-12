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

#include "hcomm/ccu/hcomm_res_ccu_entity_defs.h"

namespace asc {

/**
 * @brief CCU 批量资源类型枚举。
 * @note resTypes 数组按枚举值索引；block 与非 block 资源在 range 视图中分开表示。
 */
enum HcommCcuBatchResType {
    HCOMM_CCU_BATCH_RES_LOOP = 0,       /**< LoopEngine 资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_LOOP = 1, /**< block LoopEngine 资源 */
    HCOMM_CCU_BATCH_RES_MS = 2,         /**< Memory Slice 资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_MS = 3,   /**< block Memory Slice 资源 */
    HCOMM_CCU_BATCH_RES_CKE = 4,        /**< 完成事件/信号量资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_CKE = 5,  /**< block 完成事件/信号量资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_XN = 6,   /**< block 变量资源 */
    HCOMM_CCU_BATCH_RES_XN = 7,         /**< 变量资源 */
    HCOMM_CCU_BATCH_RES_GSA = 8,        /**< 通用存储地址资源 */
    HCOMM_CCU_BATCH_RES_BLOCK_GSA = 9,  /**< block 通用存储地址资源 */
    HCOMM_CCU_BATCH_RES_MISSION = 10,   /**< mission 资源 */
    HCOMM_CCU_BATCH_RES_TYPE_COUNT,
};

/**
 * @brief CCU 版本常量。
 * @note ABI 内透传的设备级 CCU 版本。
 */
enum HcommCcuVersion {
    HCOMM_CCU_VERSION_INVALID = -1, /**< 无效版本哨兵 */
    HCOMM_CCU_VERSION_V1 = 0,
    HCOMM_CCU_VERSION_V2 = 1,
};

constexpr uint32_t HCOMM_CCU_MAX_DIE_NUM = 2U; // 当前支持的 CCU die 数（现网 2 die；硬件扩展后调整）
constexpr uint64_t HCOMM_CCU_RESOURCE_XN_PER_SIZE = 8U; // 单个 XN 资源占用的空间步长

/** asc 本地 range 记录：wire POD（HcommCcuDieResRange 只有 startId/count）加上归属的类型与 die */
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
    // INS 不再入本地资源池：指令空间经控制操作槽位向 hcomm 按需申请，随 RegisterContext 归还
};

} // namespace asc

#endif // ASC_CCU_RESOURCE_LOCAL_H
