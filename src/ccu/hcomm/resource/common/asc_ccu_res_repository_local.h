/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_CCU_RES_REPOSITORY_LOCAL_H
#define ASC_CCU_RES_REPOSITORY_LOCAL_H

#include <array>
#include <vector>

#include "hcomm_ccu_res.h"

namespace hcomm {

using AscCcuResRanges = std::array<std::vector<HcommCcuResRangePod>, HCOMM_CCU_MAX_DIE_NUM>;

// 该聚合结构只属于 asc-comm；跨 SO 传递的仅是其中的 range POD 元素，不传递 vector 本身。
struct AscCcuResRepository {
    AscCcuResRanges loopEngine{};
    AscCcuResRanges blockLoopEngine{};
    AscCcuResRanges ms{};
    AscCcuResRanges blockMs{};
    AscCcuResRanges cke{};
    AscCcuResRanges blockCke{};
    AscCcuResRanges xn{};
    AscCcuResRanges blockXn{};
    AscCcuResRanges gsa{};
    AscCcuResRanges blockGsa{};
    AscCcuResRanges mission{};
};

} // namespace hcomm

#endif // ASC_CCU_RES_REPOSITORY_LOCAL_H
