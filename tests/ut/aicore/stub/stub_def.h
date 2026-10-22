/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_UT_STUB_DEF_H
#define ASCCOMM_UT_STUB_DEF_H

// CANN 9.3 exposes vector_fp8_e6m2_t in the public c_api type header, but its
// CPU-debug stub_def.h does not define the underlying vector types yet. Keep the
// original definitions from the CANN package and append the missing UT-only types.
#include_next "stub_def.h"

using hif4x2_t = hifloat4x2_t;

#endif // ASCCOMM_UT_STUB_DEF_H
