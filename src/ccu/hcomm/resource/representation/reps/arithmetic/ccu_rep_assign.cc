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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

void ccu_rep_assign::set_common_info()
{
    type_ = ccu_rep_type::assign;
    instr_count_ = 1;
}

ccu_rep_assign::ccu_rep_assign(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, uint64_t immediate)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(assign_sub_type::imd_to_variable), immediate_(immediate), var_a_(var_a)
{
    set_common_info();
}

ccu_rep_assign::ccu_rep_assign(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, uint64_t immediate)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(assign_sub_type::imd_to_addr), immediate_(immediate), addr_a_(addr_a)
{
    set_common_info();
}

ccu_rep_assign::ccu_rep_assign(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const variable& var_a)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(assign_sub_type::var_to_addr), immediate_(0), var_a_(var_a), addr_a_(addr_a)
{
    set_common_info();
}

ccu_rep_assign::ccu_rep_assign(ccu_ins_generater_base* ins_gen_ptr, const address& addr_b, const address& addr_a)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(assign_sub_type::addr_to_addr),
      immediate_(0),
      addr_a_(addr_a),
      addr_b_(addr_b)
{
    set_common_info();
}

ccu_rep_assign::ccu_rep_assign(ccu_ins_generater_base* ins_gen_ptr, const variable& var_b, const variable& var_a)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(assign_sub_type::var_to_var), immediate_(0), var_a_(var_a), var_b_(var_b)
{
    set_common_info();
}

bool ccu_rep_assign::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    asc::check_ccu_not_null(instr, "[CcuRepAssign::translate] instr is nullptr!");
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_assign_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepAssign][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepAssign translate failed");

    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepAssign::translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instr_id,
            instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_assign::describe()
{
    switch (sub_type_) {
        case assign_sub_type::imd_to_variable: {
            return asc::format_ccu_message("Variable[%u] = Value[%lu]", var_a_.id(), immediate_);
        }
        case assign_sub_type::imd_to_addr: {
            return asc::format_ccu_message("Address[%u] = Value[%lu]", addr_a_.id(), immediate_);
        }
        case assign_sub_type::var_to_addr: {
            return asc::format_ccu_message("Address[%u] = Variable[%u]", addr_a_.id(), var_a_.id());
        }
        case assign_sub_type::addr_to_addr: {
            return asc::format_ccu_message("Address[%u] = Address[%u]", addr_b_.id(), addr_a_.id());
        }
        case assign_sub_type::var_to_var: {
            return asc::format_ccu_message("Var[%u] = Var[%u]", var_b_.id(), var_a_.id());
        }
        default: {
            return asc::format_ccu_message("Invalid Assign");
        }
    }
}

address ccu_rep_assign::get_addr_a() { return addr_a_; }

address ccu_rep_assign::get_addr_b() { return addr_b_; }

variable ccu_rep_assign::get_var_a() { return var_a_; }

variable ccu_rep_assign::get_var_b() { return var_b_; }

uint64_t ccu_rep_assign::get_immed() const { return immediate_; }

assign_sub_type ccu_rep_assign::get_sub_type() const { return sub_type_; }
}; // namespace ccu_rep
}; // namespace asc
