/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {

ccu_kernel::~ccu_kernel() = default;

void ccu_kernel::append(std::shared_ptr<asc::ccu_rep::ccu_rep_base> rep) { asc::ccu_rep::append_to_context(this, rep); }

asc::ccu_rep::variable ccu_kernel::create_variable() { return asc::ccu_rep::variable(this); }

} // namespace asc
