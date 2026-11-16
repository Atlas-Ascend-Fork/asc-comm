/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RUNTIME_HCOMM_BACKEND_H
#define CCU_RUNTIME_HCOMM_BACKEND_H

#include "ccu_runtime_backend.h"

namespace ccu::runtime {

// The HComm POD backend is linked directly into libccu_runtime. It is an
// implementation detail and is not part of the installed runtime API.
Backend& hcommBackend() noexcept;

} // namespace ccu::runtime

#endif // CCU_RUNTIME_HCOMM_BACKEND_H
