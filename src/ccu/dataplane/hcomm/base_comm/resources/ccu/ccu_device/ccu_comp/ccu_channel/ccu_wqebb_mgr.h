/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_WQEBB_MGR_H
#define CCU_WQEBB_MGR_H

#include <memory>

#include "ccu_dev_mgr_imp.h"
#include "ccu_res_allocator.h"

namespace hcomm {

class CcuWqeBBMgr {
public:
    CcuWqeBBMgr(const int32_t devLogicId, const uint8_t dieId)
        : devLogicId_(devLogicId), dieId_(dieId) {};
    CcuWqeBBMgr() = default;
    ~CcuWqeBBMgr() = default;

    HcclResult Init();
    HcclResult Alloc(const uint32_t sqSize, ResInfo &wqeBBInfo);
    HcclResult Release(const ResInfo &wqeBBInfo);

private:
    int32_t devLogicId_{0};
    uint8_t dieId_{0};

    std::unique_ptr<CcuResIdAllocator> idAllocator_{nullptr};
};

} // namespace hccl

#endif // CCU_WQEBB_MGR_H