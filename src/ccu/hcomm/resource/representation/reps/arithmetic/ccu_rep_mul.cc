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
void ccu_rep_mul::set_common_info()
{
    type_ = ccu_rep_type::mul;
    instr_count_ = 1;
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::var_mul_var_to_var),
      var_a_(var_a),
      var_b_(var_b),
      var_c_(var_c)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::var_mul_immed_to_var),
      var_a_(var_a),
      var_c_(var_c),
      immed_b_(immed_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(mul_sub_type::self_mul_var_variable), var_a_(var_a), var_b_(var_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(mul_sub_type::self_mul_immed_variable), var_a_(var_a), immed_b_(immed_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::var_mul_var_to_addr),
      var_a_(var_a),
      var_b_(var_b),
      addr_c_(addr_c)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const address& addr_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::var_mul_addr_to_addr),
      var_a_(var_a),
      addr_b_(addr_b),
      addr_c_(addr_c)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const variable& var_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(mul_sub_type::self_mul_var_address), var_b_(var_b), addr_a_(addr_a)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::addr_mul_immed_to_addr),
      addr_a_(addr_a),
      addr_c_(addr_c),
      immed_b_(immed_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::var_mul_immed_to_addr),
      var_a_(var_a),
      addr_c_(addr_c),
      immed_b_(immed_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr), sub_type_(mul_sub_type::self_mul_immed_address), addr_a_(addr_a), immed_b_(immed_b)
{
    set_common_info();
}

ccu_rep_mul::ccu_rep_mul(
    ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const address& addr_a, const uint16_t immed_b)
    : ins_gen_ptr_(ins_gen_ptr),
      sub_type_(mul_sub_type::addr_mul_immed_to_var),
      var_c_(var_c),
      addr_a_(addr_a),
      immed_b_(immed_b)
{
    set_common_info();
}

void ccu_rep_mul::validate_ins_gen_ptr_for_mul() const
{
    ccu_ins_generater_v1* tmp_ptr_v1 = dynamic_cast<ccu_ins_generater_v1*>(ins_gen_ptr_);
    CHK_PRT_THROW(
        (tmp_ptr_v1 && !support_ccu_v1_),
        HCCL_ERROR("[CcuRepMul][%s]Cannot translate CcuRepMul for A5 when supportCcuV1 is false", __func__),
        CcuResult::CCU_E_INTERNAL, "tmpPtrV1 does not match supportCcuV1");
}

bool ccu_rep_mul::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_gen_ptr_for_mul();
    asc::check_ccu_not_null(instr, "[CcuRepMul::translate] instr is nullptr!");
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_mul_translate(ccu_kernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepMul][translate] failed to translate for instrId[%u]", instr_id), CcuResult::CCU_E_INTERNAL,
        "CcuRepMul translate failed");

    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepMul::translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instr_id,
            instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_mul::describe()
{
    switch (sub_type_) {
        case mul_sub_type::var_mul_var_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = Variable[%u] * Variable[%u]", var_c_.id(), var_a_.id(), var_b_.id());
        }
        case mul_sub_type::var_mul_immed_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = Variable[%u] * Immed[%u]", var_c_.id(), var_a_.id(), immed_b_);
        }
        case mul_sub_type::self_mul_var_variable: {
            return asc::format_ccu_message("Variable[%u] *= Variable[%u]", var_a_.id(), var_b_.id());
        }
        case mul_sub_type::self_mul_immed_variable: {
            return asc::format_ccu_message("Variable[%u] *= Immed[%u]", var_a_.id(), immed_b_);
        }
        case mul_sub_type::var_mul_var_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] = Variable[%u] * Variable[%u]", addr_c_.id(), var_a_.id(), var_b_.id());
        }
        case mul_sub_type::var_mul_addr_to_addr: {
            return asc::format_ccu_message(
                "Address[%u] = Variable[%u] * Address[%u]", addr_c_.id(), var_a_.id(), addr_b_.id());
        }
        case mul_sub_type::var_mul_immed_to_addr: {
            return asc::format_ccu_message(
                "address[%u] = Variable[%u] * Immed[%u]", addr_c_.id(), var_a_.id(), immed_b_);
        }
        case mul_sub_type::addr_mul_immed_to_addr: {
            return asc::format_ccu_message(
                "address[%u] = address[%u] * Immed[%u]", addr_c_.id(), addr_a_.id(), immed_b_);
        }
        case mul_sub_type::self_mul_var_address: {
            return asc::format_ccu_message("address[%u] *= Variable[%u]", addr_a_.id(), var_b_.id());
        }
        case mul_sub_type::self_mul_immed_address: {
            return asc::format_ccu_message("address[%u] *= Immed[%u]", addr_a_.id(), immed_b_);
        }
        case mul_sub_type::addr_mul_immed_to_var: {
            return asc::format_ccu_message(
                "Variable[%u] = address[%u] * Immed[%u]", var_c_.id(), addr_a_.id(), immed_b_);
        }
        default: {
            return asc::format_ccu_message("Invalid Mul");
        }
    }
}

address ccu_rep_mul::get_addr_a() { return addr_a_; }

address ccu_rep_mul::get_addr_b() { return addr_b_; }

address ccu_rep_mul::get_addr_c() { return addr_c_; }

variable ccu_rep_mul::get_var_a() { return var_a_; }

variable ccu_rep_mul::get_var_b() { return var_b_; }

variable ccu_rep_mul::get_var_c() { return var_c_; }

uint16_t ccu_rep_mul::get_immed_b() const { return immed_b_; }

mul_sub_type ccu_rep_mul::get_sub_type() const { return sub_type_; }
}; // namespace ccu_rep
}; // namespace asc
