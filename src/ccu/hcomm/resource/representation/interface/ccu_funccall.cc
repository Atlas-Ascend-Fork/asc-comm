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
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"

#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {
namespace ccu_rep {

func_call::func_call(ccu_rep_context* context, std::string label) : context_(context), label_(label)
{
    ccu_ins_generater_base* ins_gen_ptr = context->get_ins_generator();
    asc::check_ccu_not_null(ins_gen_ptr, "[FuncCall::FuncCall](label) insGenPtr is nullptr!");
    rep_func_call_ = std::make_shared<ccu_rep_func_call>(ins_gen_ptr, label);
}

func_call::func_call(ccu_rep_context* context, const variable& func_addr) : context_(context)
{
    ccu_ins_generater_base* ins_gen_ptr = context->get_ins_generator();
    asc::check_ccu_not_null(ins_gen_ptr, "[FuncCall::FuncCall](funcAddr) insGenPtr is nullptr!");
    rep_func_call_ = std::make_shared<ccu_rep_func_call>(ins_gen_ptr, func_addr);
}

void func_call::append_to_context()
{
    if (context_ == nullptr) {
        asc::throw_ccu_internal("context is nullptr, func call, append to context");
    }
    return context_->append(rep_func_call_);
}

}; // namespace ccu_rep
}; // namespace asc
