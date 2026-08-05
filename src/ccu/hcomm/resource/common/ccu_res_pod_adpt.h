/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RES_POD_ADPT_H
#define CCU_RES_POD_ADPT_H

#include <cstdint>

#include "ccu/hcomm/ccu_types.h"
#include "hcomm/resource/common/asc_ccu_res_repository_local.h"
#include "hcomm/hcomm_ccu_res_api.h"

namespace hcomm {

class CcuResPodAdapter {
public:
    static CcuResult Alloc(int32_t deviceLogicId, const HcommCcuResRequestPod& request, HcommCcuResHandle& handle);
    static CcuResult AllocByInstanceType(int32_t deviceLogicId, uint32_t instanceType, HcommCcuResHandle& handle);
    static CcuResult GetRepository(HcommCcuResHandle handle, AscCcuResRepository& repository);
    static CcuResult Release(HcommCcuResHandle handle);
    static CcuResult AllocInstruction(
        int32_t deviceLogicId, uint32_t dieId, uint32_t count, HcommCcuResRangePod& range);
    static CcuResult ReleaseInstruction(int32_t deviceLogicId, uint32_t dieId, const HcommCcuResRangePod& range);
};

} // namespace hcomm

#endif // CCU_RES_POD_ADPT_H
