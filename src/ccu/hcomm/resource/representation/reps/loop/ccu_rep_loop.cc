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
#include <climits>

#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

namespace asc {
namespace ccu_rep {

ccu_rep_loop::ccu_rep_loop(
    ccu_ins_generater_base* ins_generator_ptr, const std::string& label, const variable& loop_param)
    : ins_generator_ptr_(ins_generator_ptr), label_(label), loop_param_(loop_param)
{
    type_ = ccu_rep_type::loop;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
    support_ccu_v1_ = true;
}

ccu_rep_loop::ccu_rep_loop(
    ccu_ins_generater_base* ins_generator_ptr, const std::string& label, const variable& loop_param,
    const variable& loop_iter_num, const variable& loop_gsa_offset)
    : ins_generator_ptr_(ins_generator_ptr),
      label_(label),
      loop_param_(loop_param),
      loop_iter_num_(loop_iter_num),
      loop_gsa_offset_(loop_gsa_offset)
{
    type_ = ccu_rep_type::loop;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
    support_ccu_v1_ = false; // loopParam按照A6格式填写，不适用A5
}

void ccu_rep_loop::validate_ins_generator_for_loop() const
{
    ccu_ins_generater_v1* tmp_ptr_v1 = dynamic_cast<ccu_ins_generater_v1*>(ins_generator_ptr_);
    if (tmp_ptr_v1 && !support_ccu_v1_) {
        // 在A5场景下没有使用A5的loop调用方式
        asc::throw_ccu_internal("Cannot translate CcuRepLoop for A5 when supportCcuV1 is false!");
    }
}

const std::string& ccu_rep_loop::get_label() const { return label_; }

void ccu_rep_loop::reference(std::shared_ptr<ccu_rep_loop_block> ref_rep) { loop_block_ = ref_rep; }

std::shared_ptr<ccu_rep_base> ccu_rep_loop::set_loop_param(executor executor, variable var)
{
    return std::make_shared<ccu_rep_set_loop>(ins_generator_ptr_, loop_param_, executor, var);
}

bool ccu_rep_loop::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    (void)dep;
    validate_ins_generator_for_loop();

    this->instr_id_ = instr_id;
    translated_ = true;

    asc::check_ccu_not_null(loop_block_, "[CcuRepLoop::translate] LoopBlock is nullptr!");

    if (!loop_block_->translated()) {
        asc::throw_ccu_internal("Reference To Invalid LoopBlock");
    }

    uint16_t start_instr_id = loop_block_->start_instr_id();
    uint16_t loop_block_instr_count = loop_block_->instr_count();
    if (loop_block_instr_count == 0) {
        HCCL_ERROR(
            "[CcuRepLoop][translate] loopBlockInstrCount[%u] is 0, which causes underflow in endInstrId "
            "calculation.",
            loop_block_instr_count);
        return false;
    }
    if (start_instr_id > USHRT_MAX - loop_block_instr_count) {
        HCCL_ERROR(
            "[CcuRepLoop][translate] startInstrId[%u] + loopBlockInstrCount[%u] exceeds the maximum value "
            "of unsigned short int.",
            start_instr_id, loop_block_instr_count);
        return false;
    }

    if (instr_id > USHRT_MAX - instr_count_) {
        HCCL_ERROR("[CcuRepLoop][translate] instrId[%u] exceeds the maximum value of unsigned short int.", instr_id);
        return false;
    }

    uint16_t end_instr_id = start_instr_id + loop_block_instr_count - 1;

    loop_instr(instr++, start_instr_id, end_instr_id, loop_param_.id());

    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_loop::describe() { return asc::format_ccu_message("Loop reference to [%s]", label_.c_str()); }

}; // namespace ccu_rep
}; // namespace asc
