/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RES_PACK_H
#define CCU_RES_PACK_H

#include <array>
#include <cstdint>

#include "ccu/hcomm/ccu_types.h"
#include "hcomm_ccu_res.h"
#include "hcomm/resource/common/asc_ccu_res_repository_local.h"

namespace hcomm {

class CcuResPack {
public:
    CcuResPack() = default;
    ~CcuResPack() = default;

    CcuResult Load(const HcommCcuRegisterContextPod &context);
    CcuResult Reset();
    CcuResult AllocateInstruction(uint32_t dieId, uint32_t count, HcommCcuResRangePod &range);

    AscCcuResRepository &GetCcuResRepo();
    const AscCcuResRepository &GetTranslatorResRepo() const;
    const HcommCcuDieMetadataPod *GetDieMetadata(uint32_t dieId) const;
    uint64_t GetGeneration() const;
    int32_t GetDeviceLogicId() const;
    uint32_t GetCcuVersion() const;
    uint32_t GetValidDieMask() const;

private:
    CcuResPack(const CcuResPack &that) = delete;
    CcuResPack &operator=(const CcuResPack &that) = delete;
    CcuResPack(CcuResPack &&that) = delete;
    CcuResPack &operator=(CcuResPack &&that) = delete;

    uint64_t generation_{0};
    int32_t deviceLogicId_{0};
    uint32_t ccuVersion_{0};
    uint32_t validDieMask_{0};
    AscCcuResRepository initialRepo_{};
    AscCcuResRepository resRepo_{};
    AscCcuResRepository translatorRepo_{};
    std::array<HcommCcuResRangePod, HCOMM_CCU_MAX_DIE_NUM> instructionPools_{};
    std::array<uint32_t, HCOMM_CCU_MAX_DIE_NUM> instructionUsed_{};
    std::array<HcommCcuDieMetadataPod, HCOMM_CCU_MAX_DIE_NUM> dieMetadata_{};
};

} // namespace hcomm

#endif // CCU_RES_PACK_H
