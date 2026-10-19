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

namespace float8_e6m2 {
struct Fp8e6m2T {
    uint8_t val;
};
} // namespace float8_e6m2
using fp8_e6m2_t = float8_e6m2::Fp8e6m2T;
using vector_f8e6m2 = std::array<fp8_e6m2_t, ConstantsInternal::b8EleSize>;
using vector_fp8_e6m2_t = vector_f8e6m2;

struct hifloat4x2_t {
    uint8_t val = 0;
};
using hif4x2_t = hifloat4x2_t;

using vector_hif4x2 = std::array<hifloat4x2_t, ConstantsInternal::b8EleSize>;

#endif // ASCCOMM_UT_STUB_DEF_H
