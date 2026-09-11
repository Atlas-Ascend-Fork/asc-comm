/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

condition::condition(ccu_rep_context* context, ccu_relational_operator<variable, uint64_t> rel) : context_(context)
{
    std::string label = "Condition";
}

condition::~condition() { append_to_context(context_, end_label_); }

bool condition::check() const { return !is_executed_; }

void condition::run() { is_executed_ = true; }

}; // namespace ccu_rep
}; // namespace asc
