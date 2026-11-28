/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_api/resource/representation/ccu_rep_v1.h"
#include "ccu_api/common/ccu_exception.h"
#include "ccu_api/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {
namespace ccu_rep {

ccu_rep_write_with_cnt_inc::ccu_rep_write_with_cnt_inc(
    ccu_ins_generater_base* ins_gen_ptr, variable& channel, remote_addr& rem, local_addr& loc, variable& len,
    remote_addr& inc_cnt, variable& orig_len)
    : ins_generator_ptr_(ins_gen_ptr),
      channel_(channel),
      rem_(rem),
      loc_(loc),
      len_(len),
      inc_cnt_(inc_cnt),
      orig_len_(orig_len)
{
    type_ = ccu_rep_type::write_with_cnt_inc;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
}

bool ccu_rep_write_with_cnt_inc::translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;
    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_write_with_cnt_inc_translate(ccu_kernel, instr, instr_id, this, dep) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepWriteWithCntInc][translate] failed to translate for instrId[%u]", instr_id),
        ccu_result::CCU_E_INTERNAL, "CcuRepWriteWithCntInc translate failed");
    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepWriteWithCntInc][translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]",
            instr_id, instr_count_),
        ccu_result::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_write_with_cnt_inc::describe()
{
    return asc::format_ccu_message(
        "Write With Cnt Inc, rem[%u] to loc[%u], len[%u], incCntAddr[%u], origLen[%u]", rem_.addr.id(), loc_.addr.id(),
        len_.id(), inc_cnt_.addr.id(), orig_len_.id());
}

}; // namespace ccu_rep
}; // namespace asc
