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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_buf_reduce::ccu_rep_buf_reduce(
    ccu_ins_generater_base* ins_gen_ptr, const std::vector<ccu_buf>& mem, uint16_t count, uint16_t data_type,
    uint16_t output_data_type, uint16_t op_type, completed_event sem, const ccu_rep::variable& len, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr),
      mem_(mem),
      count_(count),
      data_type_(data_type),
      output_data_type_(output_data_type),
      op_type_(op_type),
      sem_(sem),
      xn_id_length_(len),
      mask_(mask)
{
    type_ = ccu_rep_type::buf_reduce;
    instr_count_ = 1;
}

bool ccu_rep_buf_reduce::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    instr_count_ = ins_gen_ptr_->get_instr_count(type_);
    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_buf_reduce_translate(ccu_kernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepBufReduce][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepBufReduce translate failed");

    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_buf_reduce::describe() { return asc::format_ccu_message("Reduce"); }

}; // namespace ccu_rep
}; // namespace asc
