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
#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/resource/common/asc_ccu_resource_local.h"

namespace asccomm {

/**
 * @note 职责：RegisterContext 的 asc 侧本地快照。
 *
 * instance 资源实体由 hcomm 的 RegisterContext 持有与释放，本类只保存其快照，不持有任何跨 SO 句柄。
 * resRepo_ 是被 kernel 注册消耗的余量池（见 MoveResInfo）。
 *
 * 命名以 Asc 前缀区分：hcomm legacy 另有同名的 struct CcuResPack
 * （legacy/ascend950/unified_platform/pub_inc/ccu/ccu_res_pack.h），二者无关。
 */
class AscCcuResSnapshot {
public:
    AscCcuResSnapshot() = default;
    ~AscCcuResSnapshot() = default;

    CcuResult Load(const HcommCcuRegisterContextPod &context);
    CcuResult Reset();

    AscCcuResRepository &GetCcuResRepo();
    const HcommCcuDieMetadataPod *GetDieMetadata(uint32_t dieId) const;
    uint64_t GetGeneration() const;
    int32_t GetDeviceLogicId() const;
    uint32_t GetCcuVersion() const;
    uint32_t GetValidDieMask() const;
    const HcommCcuControlOpsPod &GetControlOps() const;

private:
    AscCcuResSnapshot(const AscCcuResSnapshot &that) = delete;
    AscCcuResSnapshot &operator=(const AscCcuResSnapshot &that) = delete;
    AscCcuResSnapshot(AscCcuResSnapshot &&that) = delete;
    AscCcuResSnapshot &operator=(AscCcuResSnapshot &&that) = delete;

    uint64_t generation_{0};
    int32_t deviceLogicId_{0};
    uint32_t ccuVersion_{0};
    uint32_t validDieMask_{0};
    HcommCcuControlOpsPod controlOps_{};
    AscCcuResRepository initialRepo_{};
    AscCcuResRepository resRepo_{};
    std::array<HcommCcuDieMetadataPod, HCOMM_CCU_MAX_DIE_NUM> dieMetadata_{};
};

} // namespace asccomm

#endif // ASC_CCU_RES_SNAPSHOT_H
