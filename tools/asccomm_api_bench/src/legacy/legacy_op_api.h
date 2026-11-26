/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef MC2_BENCH_LEGACY_OP_API_H
#define MC2_BENCH_LEGACY_OP_API_H

// Only the generated ACLNN name and the added runtime attributes differ.
#ifdef MC2_BENCH_CCU_BACKEND
#include "aclnn_all_gather_ccu_bench.h"
#include "aclnn_all_reduce_ccu_bench.h"
#include "aclnn_reduce_scatter_ccu_bench.h"
#include "aclnn_all_to_all_ccu_bench.h"
#include "aclnn_all_to_all_v_ccu_bench.h"
#define MC2_LEGACY_PREPARE(name) aclnn##name##CcuBenchGetWorkspaceSize
#define MC2_LEGACY_EXECUTE(name) aclnn##name##CcuBench
#define MC2_LEGACY_ENGINE , static_cast<int64_t>(options->accelerator_mode)
#else
#include "aclnn_all_gather_custom_v3.h"
#include "aclnn_all_reduce_custom_v3.h"
#include "aclnn_reduce_scatter_custom_v3.h"
#include "aclnn_all_to_all_custom_v3.h"
#include "aclnn_all_to_all_v_custom_v3.h"
#define MC2_LEGACY_PREPARE(name) aclnn##name##CustomV3GetWorkspaceSize
#define MC2_LEGACY_EXECUTE(name) aclnn##name##CustomV3
#define MC2_LEGACY_ENGINE
#endif
#endif
