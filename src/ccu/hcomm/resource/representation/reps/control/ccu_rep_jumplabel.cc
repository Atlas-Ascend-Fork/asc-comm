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

#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {
namespace ccu_rep {

ccu_rep_jump_label::ccu_rep_jump_label(ccu_ins_generater_base* ins_generator_ptr, const std::string& label)
    : ccu_rep_block(ins_generator_ptr, label)
{
    type_ = ccu_rep_type::jump_label;
    append(std::make_shared<ccu_rep_nop>(ins_generator_ptr));
}

std::string ccu_rep_jump_label::describe() { return asc::format_ccu_message("JumpLabel[%s]", get_label().c_str()); }

}; // namespace ccu_rep
}; // namespace asc
