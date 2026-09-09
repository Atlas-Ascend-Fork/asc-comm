/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_dev_mgr_imp.h"

namespace asc {

HcclResult CcuDevMgrImp::GetXnBaseAddr(const uint32_t dev_logic_id, const uint8_t dieId, uint64_t& xnBaseAddr)
{
    (void)dev_logic_id;
    (void)dieId;
    xnBaseAddr = 0;
    return HcclResult::HCCL_SUCCESS;
}

} // namespace asc
