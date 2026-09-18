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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

namespace asc {
namespace ccu_rep {

// jump基类
ccu_rep_jump_base::ccu_rep_jump_base(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id)
    : ins_generator_ptr_(ins_gen_ptr), label_(label), target_instr_id_(target_instr_id)
{}

ccu_rep_jump_base::ccu_rep_jump_base(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition)
    : ins_generator_ptr_(ins_gen_ptr),
      label_(label),
      target_instr_id_(target_instr_id),
      expected_var_(expected_var),
      condition_(condition)
{}

void ccu_rep_jump_base::reference(std::shared_ptr<ccu_rep_jump_label> ref_rep) { jump_label_ = ref_rep; }

void ccu_rep_jump_base::validate_ins_generator_for_jump()
{
    ccu_ins_generater_v1* tmp_ptr_v1 = dynamic_cast<ccu_ins_generater_v1*>(ins_generator_ptr_);
    if (tmp_ptr_v1 && !support_ccu_v1_) {
        // 当右值只传入var没有立即数时，无法在A5上翻译
        asc::throw_ccu_internal("Cannot translate %s for A5 when supportCcuV1 is false!", this->describe().c_str());
    }
}

CcuResult ccu_rep_jump_base::init_instr(ccu_instr*& instr, uint16_t& instr_id)
{
    CCU_CHK_PTR_NULL(instr);
    if (this->instr_ == nullptr) {
        this->instr_id_ = instr_id;
        this->instr_ = instr;
        instr += instr_count_;
        instr_id += instr_count_;
    }
    return CcuResult::CCU_SUCCESS;
}

// direct jump
ccu_rep_jump::ccu_rep_jump(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id)
{
    type_ = ccu_rep_type::jump;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
}

bool ccu_rep_jump::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    if (init_instr(instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_translate(ccu_kernel, instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJump][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJump translate failed");
        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump::describe() { return asc::format_ccu_message("Jump To Label[%s]", label_.c_str()); }

// jumpNE
ccu_rep_jump_ne::ccu_rep_jump_ne(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_ne;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
    comp2_immed_ = true; // A6翻译时插入一条加载立即数指令，A5无关
    support_ccu_v1_ = true;
}

ccu_rep_jump_ne::ccu_rep_jump_ne(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    // 仅用于A6翻译
    type_ = ccu_rep_type::jump_ne;
    instr_count_ = 2; // 2条指令，暂直接填充指令数，insGenerator中未记录这种使用方式对应的指令数
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_ne::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();

    if (init_instr(instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_ne_translate(ccu_kernel, instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpNE][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpNE translate failed");
        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_ne::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] Not equal to Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}

// jumpEQ
ccu_rep_jump_eq::ccu_rep_jump_eq(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_eq;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
    comp2_immed_ = true; // A6翻译时插入一条加载立即数指令，A5无关
    support_ccu_v1_ = true;
}

ccu_rep_jump_eq::ccu_rep_jump_eq(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    type_ = ccu_rep_type::jump_eq;
    instr_count_ = 2; // 2条指令，暂直接填充指令数，insGenerator中未记录这种使用方式对应的指令数
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_eq::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();
    if (init_instr(instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_eq_translate(ccu_kernel, instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpEQ][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpEQ translate failed");
        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_eq::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] Be equal to Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}

// jumpLE
ccu_rep_jump_le::ccu_rep_jump_le(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    type_ = ccu_rep_type::jump_le;
    instr_count_ = 2; // 2条指令
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

ccu_rep_jump_le::ccu_rep_jump_le(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_le;
    instr_count_ = 3; // 3条指令
    comp2_immed_ = true;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_le::translate(ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();

    if (init_instr(cur_instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_le_translate(ccu_kernel, cur_instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpLE][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpLE translate failed");
        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_le::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] <= Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}

// jumpGE
ccu_rep_jump_ge::ccu_rep_jump_ge(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    type_ = ccu_rep_type::jump_ge;
    instr_count_ = 2; // 2条指令
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

ccu_rep_jump_ge::ccu_rep_jump_ge(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_ge;
    instr_count_ = 3; // 3条指令
    comp2_immed_ = true;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_ge::translate(ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();

    if (init_instr(cur_instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_ge_translate(ccu_kernel, cur_instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpGE][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpGE translate failed");

        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_ge::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] >= Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}

// jumpGT
ccu_rep_jump_gt::ccu_rep_jump_gt(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    type_ = ccu_rep_type::jump_gt;
    instr_count_ = 2; // 2条指令
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

ccu_rep_jump_gt::ccu_rep_jump_gt(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_gt;
    instr_count_ = 3; // 3条指令
    comp2_immed_ = true;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_gt::translate(ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();
    if (init_instr(cur_instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }
    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_gt_translate(ccu_kernel, cur_instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpGT][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpGT translate failed");

        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_gt::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] > Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}

// jumpLT
ccu_rep_jump_lt::ccu_rep_jump_lt(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& condition, const variable& expected_var)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    type_ = ccu_rep_type::jump_lt;
    instr_count_ = 2; // 2条指令
    comp2_immed_ = false;
    support_ccu_v1_ = false;
}

ccu_rep_jump_lt::ccu_rep_jump_lt(
    ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
    const variable& expected_var, const variable& condition, uint64_t expected)
    : ccu_rep_jump_base(ins_gen_ptr, label, target_instr_id, expected_var, condition)
{
    this->expected_ = expected;
    type_ = ccu_rep_type::jump_lt;
    instr_count_ = 3; // 3条指令
    comp2_immed_ = true;
    support_ccu_v1_ = false;
}

bool ccu_rep_jump_lt::translate(ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_jump();
    if (init_instr(cur_instr, instr_id) != CcuResult::CCU_SUCCESS) {
        asc::throw_ccu_internal("instr is empty!");
    }

    if (jump_label_->translated()) {
        CHK_PRT_THROW(
            ins_generator_ptr_->ccu_rep_jump_lt_translate(ccu_kernel, cur_instr, instr_id, this, dep) !=
                CcuResult::CCU_SUCCESS,
            HCCL_ERROR("[CcuRepJumpLT][translate] failed to translate for instrId[%u]", instr_id),
            CcuResult::CCU_E_INTERNAL, "CcuRepJumpLT translate failed");

        translated_ = true;
    }

    return translated_;
}

std::string ccu_rep_jump_lt::describe()
{
    return asc::format_ccu_message(
        "Jump To Label[%s], When Condition[%u] < Expected[%lu]", label_.c_str(), condition_.id(), expected_);
}
}; // namespace ccu_rep
}; // namespace asc
