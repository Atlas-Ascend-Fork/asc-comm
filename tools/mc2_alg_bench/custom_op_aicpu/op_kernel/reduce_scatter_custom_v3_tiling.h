/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_API_TEST_REDUCE_SCATTER_TILING_H
#define HCCL_API_TEST_REDUCE_SCATTER_TILING_H

#include <cstdint>
#include "kernel_tiling/kernel_tiling.h"

struct ReduceScatterPerfTiling {
    uint64_t count;
    uint32_t reduceType;
    uint32_t dataType;
};

class ReduceScatterCustomV3TilingData {
public:
    Mc2InitTiling mc2InitTiling;
    Mc2CcTiling mc2CcTiling;
    ReduceScatterPerfTiling param;
};

#endif
