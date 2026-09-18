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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

ccu_rep_loc_cpy::ccu_rep_loc_cpy(
    ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len, completed_event sem,
    uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr), dst_(dst), src_(src), len_(len), sem_(sem), mask_(mask)
{
    type_ = ccu_rep_type::local_cpy;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
    use_ccu_buffer_ = false;
}

ccu_rep_loc_cpy::ccu_rep_loc_cpy(
    ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len, uint16_t data_type,
    uint16_t op_type, completed_event sem, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr),
      dst_(dst),
      src_(src),
      len_(len),
      sem_(sem),
      mask_(mask),
      data_type_(data_type),
      op_type_(op_type)
{
    type_ = ccu_rep_type::local_reduce;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
    reduce_flag_ = 1;
    // A5和A6都走环回
    use_ccu_buffer_ = false;
}

ccu_rep_loc_cpy::ccu_rep_loc_cpy(
    ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len, const std::vector<ccu_buf>& bufs,
    completed_event sem, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr), dst_(dst), src_(src), len_(len), bufs_(bufs), sem_(sem), mask_(mask)
{
    type_ = ccu_rep_type::local_cpy;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
    use_ccu_buffer_ = true;
}

void ccu_rep_loc_cpy::validate_ins_generator_for_loc_cpy() const
{
    ccu_ins_generater_v1* tmp_ptr_v1 = dynamic_cast<ccu_ins_generater_v1*>(ins_gen_ptr_);
    if (tmp_ptr_v1 && use_ccu_buffer_) {
        // 使用了A6场景的ms中转搬运
        asc::throw_ccu_internal("Cannot translate CcuRepLocCpy for A5 when useCcuBuffer is true!");
    }
}

uint16_t ccu_rep_loc_cpy::get_first_buf_id()
{
    if (bufs_.size() == 0) {
        asc::throw_ccu_internal("The length of CcuBuffer is 0!");
    }
    return bufs_[0].id();
}

uint16_t ccu_rep_loc_cpy::get_used_buf_num() { return bufs_.size(); }

bool ccu_rep_loc_cpy::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    validate_ins_generator_for_loc_cpy();

    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_loc_cpy_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepLocCpy][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepLocCpy translate failed");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_loc_cpy::describe()
{
    return asc::format_ccu_message(
        "Read LocalAddr[%u] to LocalAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
        src_.addr.id(), dst_.addr.id(), len_.id(), sem_.id(), mask_, data_type_, op_type_);
}

}; // namespace ccu_rep
}; // namespace asc
