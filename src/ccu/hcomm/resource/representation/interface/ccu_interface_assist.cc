/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

void append_to_context(ccu_rep_context* context, std::shared_ptr<ccu_rep::ccu_rep_base> rep)
{
    if (context == nullptr) {
        asc::throw_ccu_internal("context is nullptr, AppendToContext assist[%d]", rep->type());
    } else {
        return context->append(rep);
    }
}

std::shared_ptr<ccu_rep::ccu_rep_block> current_block(ccu_rep_context* context)
{
    if (context == nullptr) {
        asc::throw_ccu_internal("context is nullptr, currentBlock");
    }
    return context->current_block();
}

void set_current_block(ccu_rep_context* context, std::shared_ptr<ccu_rep::ccu_rep_block> rep_block)
{
    if (context == nullptr) {
        asc::throw_ccu_internal("context is nullptr, set currentBlock");
    }
    context->set_current_block(rep_block);
}

variable create_variable(ccu_rep_context* context)
{
    if (context == nullptr) {
        asc::throw_ccu_internal("context is nullptr, CreateVar");
    }
    auto ctx = dynamic_cast<ccu_kernel*>(context);
    if (ctx == nullptr) {
        asc::throw_ccu_internal("Invalid context");
    }
    return ctx->create_variable();
}

}; // namespace ccu_rep
}; // namespace asc
