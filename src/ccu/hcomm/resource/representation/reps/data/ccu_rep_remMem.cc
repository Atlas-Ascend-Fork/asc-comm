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
#include "hcomm/resource/microcode/ccu_assist_v1.h"

#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_rem_mem::ccu_rep_rem_mem(ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, remote_addr rem)
    : ins_gen_ptr_(ins_gen_ptr), channel_(channel), rem_(rem)
{
    type_ = ccu_rep_type::rem_mem;
    instr_count_ = 2; // 指令数为2个
}

bool ccu_rep_rem_mem::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    instr_count_ = ins_gen_ptr_->get_instr_count(type_);
    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_rem_mem_translate(ccu_kernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepRemMem][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepRemMem translate failed");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_rem_mem::describe()
{
    return asc::format_ccu_message("Get Remote Buffer Addr and TokenInfo By Transport");
}

}; // namespace ccu_rep
}; // namespace asc
