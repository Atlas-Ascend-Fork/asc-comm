/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_CCU_RES_SNAPSHOT_H
#define ASC_CCU_RES_SNAPSHOT_H

#include <array>
#include <cstdint>

#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/hcomm_ccu_resource.h"
#include "hcomm/resource/common/asc_ccu_resource_local.h"

namespace asc {

/**
 * @note 职责：CCU Instance（Register Context）的 asc 侧本地快照。
 *
 * instance 资源实体由 hcomm 的 RegisterContext 持有与释放，本类只保存其快照，不持有任何跨 SO 句柄。
 * resRepo_ 是被 kernel 注册消耗的余量池（见 MoveResInfo）。
 *
 * 命名以 Asc 前缀区分：hcomm legacy 另有同名的 struct CcuResPack
 * （legacy/ascend950/unified_platform/pub_inc/ccu/ccu_res_pack.h），二者无关。
 */
class asc_ccu_res_snapshot {
public:
    asc_ccu_res_snapshot() = default;
    ~asc_ccu_res_snapshot() = default;

    CcuResult load(const HcommCcuInstance& instance);
    CcuResult reset();

    asc_ccu_res_repository& get_ccu_res_repo();
    const HcommCcuDieMetadata* get_die_metadata(uint32_t die_id) const;
    uint32_t get_ccu_version() const;
    uint32_t get_valid_die_mask() const;
    const uint64_t* get_control_ops() const; // ascCustom 槽位数组（值需按槽位含义强转）

private:
    asc_ccu_res_snapshot(const asc_ccu_res_snapshot& that) = delete;
    asc_ccu_res_snapshot& operator=(const asc_ccu_res_snapshot& that) = delete;
    asc_ccu_res_snapshot(asc_ccu_res_snapshot&& that) = delete;
    asc_ccu_res_snapshot& operator=(asc_ccu_res_snapshot&& that) = delete;

    uint32_t ccu_version_{0};
    uint32_t valid_die_mask_{0};
    uint64_t control_ops_[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]{};
    asc_ccu_res_repository initial_repo_{};
    asc_ccu_res_repository res_repo_{};
    std::array<HcommCcuDieMetadata, HCOMM_CCU_MAX_DIE_NUM> die_metadata_{};
};

} // namespace asc

#endif // ASC_CCU_RES_SNAPSHOT_H
