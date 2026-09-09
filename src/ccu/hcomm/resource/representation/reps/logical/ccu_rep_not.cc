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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
namespace asc {
namespace ccu_rep {

ccu_rep_not::ccu_rep_not(ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_b)
    : sub_type_(not_sub_type::var_equals_not_var), var_b_(var_b), var_c_(var_c), ins_gen_ptr_(ins_gen_ptr)
{
    type_ = ccu_rep_type::not_op;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
}

bool ccu_rep_not::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, const trans_dep& dep)
{
    asc::check_ccu_not_null(instr, "[CcuRepNot::translate] instr is nullptr!");
    this->instr_id_ = cur_instr_id;
    translated_ = true;
    instr_count_ = ins_gen_ptr_->get_instr_count(type_);
    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_not_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepNot][translate] failed to translate for instrId[%u]", instr_id_), CcuResult::CCU_E_INTERNAL,
        "CcuRepNot translate failed");
    CHK_PRT_THROW(
        (cur_instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepNot::translate]uint16 integer overflow occurs, curInstrId = [%hu], instrCount = [%hu]",
            cur_instr_id, instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    cur_instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_not::describe()
{
    switch (sub_type_) {
        case not_sub_type::var_equals_not_var: {
            return asc::format_ccu_message("Variable[%u] = ~Variable[%u]", var_c_.id(), var_b_.id());
        }
        default: {
            return asc::format_ccu_message("Invalid Not");
        }
    }
}

}; // namespace ccu_rep
}; // namespace asc
