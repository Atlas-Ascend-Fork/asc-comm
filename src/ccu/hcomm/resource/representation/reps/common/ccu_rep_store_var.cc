/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/common/ccu_rep_store_var_v1.h"
#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_store_var::ccu_rep_store_var(
    ccu_ins_generater_base* ins_generator_ptr, const variable& var, const variable& dst, uint32_t num, bool hscb_flag)
    : ins_generator_ptr_(ins_generator_ptr), var_(var), dst_(dst), num_(num), hscb_flag_(hscb_flag)
{
    // for A6 only
    type_ = ccu_rep_type::store_var;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
}

bool ccu_rep_store_var::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_store_var_translate(ccu_kernel, instr, instr_id, this, dep) !=
            CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepStoreVar][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepStoreVar translate failed");

    instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_store_var::describe()
{
    return asc::format_ccu_message("Store Var([%u], [%u], [%u])", var_.id(), dst_.id(), num_);
}

}; // namespace ccu_rep
}; // namespace asc
