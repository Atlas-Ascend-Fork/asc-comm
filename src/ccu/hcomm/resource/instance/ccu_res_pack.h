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
#include <utility>
#include <vector>

#include "ccu/hcomm/ccu_types.h"
#include "ccu_device_pub.h"

namespace hcomm {

struct CcuResDescSnapshot {
    uint32_t dieId{CCU_MAX_IODIE_NUM};
    std::array<uint32_t, 8> resNum{};
};

class CcuResPack {
public:
    explicit CcuResPack(CcuInstanceType insType) : insType_(insType) {};
    explicit CcuResPack(std::vector<CcuResDescSnapshot> resDescs) : resDescs_(std::move(resDescs)) {};
    ~CcuResPack();

    CcuResult Init();
    CcuResult Reset();

    CcuResRepository& GetCcuResRepo();

private:
    CcuResPack(const CcuResPack& that) = delete;
    CcuResPack& operator=(const CcuResPack& that) = delete;
    CcuResPack(CcuResPack&& that) = delete;
    CcuResPack& operator=(CcuResPack&& that) = delete;

    int32_t devLogicId_{0};
    CcuInstanceType insType_{CcuInstanceType::CCU_UNUSED};
    std::vector<CcuResDescSnapshot> resDescs_{};
    CcuResHandle resHandle_{nullptr};
    CcuResRepository resRepo_{};
};

} // namespace hcomm

#endif // CCU_RES_PACK_H
