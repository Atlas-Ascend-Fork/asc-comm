/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/interface/ccu_loopcall_v1.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

loop_call::loop_call(ccu_rep_context* context, const std::string& label) : context_(context), label_(label)
{
    // repLoopCall = std::make_shared<CcuRepLoopCall>(label);
}

void loop_call::append_to_context()
{
    if (context_ == nullptr) {
        asc::throw_ccu_internal("context is nullptr, loopCall");
    }
    return context_->append(rep_loop_call_);
}

}; // namespace ccu_rep
}; // namespace asc
