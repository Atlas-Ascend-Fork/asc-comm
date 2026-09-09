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

ccu_rep_record_shared_notify::ccu_rep_record_shared_notify(
    ccu_ins_generater_base* ins_gen_ptr, const local_notify& notify, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr), notify_(notify), mask_(mask)
{
    type_ = ccu_rep_type::record_shared_notify;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
}

bool ccu_rep_record_shared_notify::translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_record_shared_notify_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepRecordSharedNotify][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepRecordSharedNotify translate failed");
    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepRecordSharedNotify::translate]uint16 integer overflow occurs, "
            "instrId = [%hu], instrCount = [%hu]",
            instr_id, instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_record_shared_notify::describe()
{
    return asc::format_ccu_message("Post, Use semIndex[%u] and mask[%04x]", notify_.id(), mask_);
}

}; // namespace ccu_rep
}; // namespace asc
