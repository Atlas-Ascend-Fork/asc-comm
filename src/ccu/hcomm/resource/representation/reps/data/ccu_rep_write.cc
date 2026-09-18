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
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {
namespace ccu_rep {

ccu_rep_write::ccu_rep_write(
    ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, remote_addr rem, local_addr loc, variable len,
    completed_event sem, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr), channel_(channel), rem_(rem), loc_(loc), len_(len), sem_(sem), mask_(mask)
{
    type_ = ccu_rep_type::write;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
}

ccu_rep_write::ccu_rep_write(
    ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, remote_addr rem, local_addr loc, variable len,
    uint16_t data_type, uint16_t op_type, completed_event sem, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr),
      channel_(channel),
      rem_(rem),
      loc_(loc),
      len_(len),
      sem_(sem),
      mask_(mask),
      data_type_(data_type),
      op_type_(op_type),
      reduce_flag_(1)
{
    type_ = ccu_rep_type::write;
    instr_count_ = 1;
}

bool ccu_rep_write::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_write_translate(ccu_kernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepWrite][translate] failed to translate for instrId[%u]", instr_id), CcuResult::CCU_E_INTERNAL,
        "CcuRepWrite translate failed");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_write::describe()
{
    return asc::format_ccu_message(
        "Write RemoteAddr[%u] to LocalAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
        loc_.addr.id(), rem_.addr.id(), len_.id(), sem_.id(), mask_, data_type_, op_type_);
}

}; // namespace ccu_rep
}; // namespace asc
