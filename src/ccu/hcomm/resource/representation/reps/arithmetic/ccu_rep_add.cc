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

void ccu_rep_add::set_common_info()
{
    type_ = ccu_rep_type::add;
    instr_count_ = 1;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::addr_plus_var_to_addr),
      addr_a_(addr_a),
      addr_c_(addr_c),
      var_b_(var_b)
{
    set_common_info();
    support_ccu_v1_ = true;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const address& addr_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::addr_plus_addr_to_addr),
      addr_a_(addr_a),
      addr_b_(addr_b),
      addr_c_(addr_c)
{
    set_common_info();
    support_ccu_v1_ = true;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::var_plus_var_to_var),
      var_a_(var_a),
      var_b_(var_b),
      var_c_(var_c)
{
    set_common_info();
    support_ccu_v1_ = true;
}

ccu_rep_add::ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const variable& offset)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(add_sub_type::self_add_address), addr_a_(addr_a), var_b_(offset)
{
    set_common_info();
    support_ccu_v1_ = true;
}

ccu_rep_add::ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const variable& offset)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(add_sub_type::self_add_variable), var_a_(var_a), var_b_(offset)
{
    set_common_info();
    support_ccu_v1_ = true;
}

ccu_rep_add::ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(add_sub_type::self_add_immed_variable), var_a_(var_a), immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::var_plus_immed_to_var),
      var_a_(var_a),
      var_c_(var_c),
      immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::addr_plus_immed_to_addr),
      addr_a_(addr_a),
      addr_c_(addr_c),
      immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(add_sub_type::self_add_immed_address), addr_a_(addr_a), immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::var_plus_var_to_addr),
      addr_c_(addr_c),
      var_a_(var_a),
      var_b_(var_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::var_plus_immed_to_addr),
      addr_c_(addr_c),
      var_a_(var_a),
      immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const address& addr_a, const address& addr_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::addr_plus_addr_to_var),
      addr_a_(addr_a),
      addr_b_(addr_b),
      var_c_(var_c)
{
    set_common_info();
    support_ccu_v1_ = false;
}

ccu_rep_add::ccu_rep_add(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(add_sub_type::addr_plus_immed_to_var),
      addr_a_(addr_a),
      var_c_(var_c),
      immed_b_(immed_b)
{
    set_common_info();
    support_ccu_v1_ = false;
}

void ccu_rep_add::validate_ins_gen_ptr_for_add() const
{
    ccu_ins_generater_v1* tmp_ptr_v1 = dynamic_cast<ccu_ins_generater_v1*>(ins_gen_ptr_);
    CHK_PRT_THROW(
        (tmp_ptr_v1 && !support_ccu_v1_),
        HCCL_ERROR("[CcuRepAdd][%s]Cannot translate CcuRepAdd for A5 when supportCcuV1 is false", __func__),
        CcuResult::CCU_E_INTERNAL, "tmpPtrV1 does not match supportCcuV1");
}

bool ccu_rep_add::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_gen_ptr_for_add();
    asc::check_ccu_not_null(instr, "[CcuRepAdd::translate] instr is nullptr!");
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_add_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepAdd][translate] failed to translate for instrId[%u]", instr_id), CcuResult::CCU_E_INTERNAL,
        "CcuRepAdd translate failed");
    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepAdd::translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instr_id,
            instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_add::describe()
{
    switch (sub_type_) {
        case add_sub_type::addr_plus_var_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] = Address[%u] + Variable[%u]", addr_c_.id(), addr_a_.id(), var_b_.id());
        }
        case add_sub_type::addr_plus_addr_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] = Address[%u] + Address[%u]", addr_c_.id(), addr_a_.id(), addr_b_.id());
        }
        case add_sub_type::var_plus_var_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = Variable[%u] + Variable[%u]", var_c_.id(), var_a_.id(), var_b_.id());
        }
        case add_sub_type::self_add_address: {
            return asc::format_ccu_message("Address[%u] += Variable[%u]", addr_a_.id(), var_b_.id());
        }
        case add_sub_type::self_add_variable: {
            return asc::format_ccu_message("Variable[%u] += Variable[%u]", var_a_.id(), var_b_.id());
        }
        case add_sub_type::self_add_immed_variable: {
            return asc::format_ccu_message("Variable[%u] += Immed[%u]", var_a_.id(), immed_b_);
        }
        case add_sub_type::var_plus_immed_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = Variable[%u] + Immed[%u]", var_c_.id(), var_a_.id(), immed_b_);
        }
        case add_sub_type::addr_plus_immed_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] += Address[%u] + Immed[%hu]", addr_c_.id(), addr_a_.id(), immed_b_);
        }
        case add_sub_type::var_plus_var_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] = Variable[%u] + Variable[%u]", addr_c_.id(), var_a_.id(), var_b_.id());
        }
        case add_sub_type::self_add_immed_address: {
            return asc::format_ccu_message("Address[%u] += Immed[%u]", addr_a_.id(), immed_b_);
        }
        case add_sub_type::var_plus_immed_to_addr: {
            return asc::format_ccu_message("Address[%u] += varA[%u] + Immed[%hu]", addr_c_.id(), var_a_.id(), immed_b_);
        }
        case add_sub_type::addr_plus_immed_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = Address[%u] + Immed[%u]", var_c_.id(), addr_a_.id(), immed_b_);
        }
        default: {
            return asc::format_ccu_message("Invalid Add");
        }
    }
}

address ccu_rep_add::get_addr_a() { return addr_a_; }

address ccu_rep_add::get_addr_b() { return addr_b_; }

address ccu_rep_add::get_addr_c() { return addr_c_; }

variable ccu_rep_add::get_var_a() { return var_a_; }

variable ccu_rep_add::get_var_b() { return var_b_; }

variable ccu_rep_add::get_var_c() { return var_c_; }

uint16_t ccu_rep_add::get_immed_b() const { return immed_b_; }

add_sub_type ccu_rep_add::get_sub_type() const { return sub_type_; }
}; // namespace ccu_rep
}; // namespace asc
