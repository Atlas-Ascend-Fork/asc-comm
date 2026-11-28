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

ccu_rep_store_add_imm::ccu_rep_store_add_imm(
    ccu_ins_generater_base* ins_gen_ptr, variable& dst, uint16_t dst_num, variable& dst_offset, uint16_t imm_add_value,
    variable& src)
    : ins_generator_ptr_(ins_gen_ptr),
      dst_(dst),
      dst_offset_(dst_offset),
      src_(src),
      imm_add_value_(imm_add_value),
      dst_num_(dst_num)
{
    type_ = ccu_rep_type::store_add_imm;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
}

bool ccu_rep_store_add_imm::translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;
    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_store_add_imm_translate(ccu_kernel, instr, instr_id, this, dep) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepStoreAddImm][translate] failed to translate for instrId[%u]", instr_id),
        ccu_result::CCU_E_INTERNAL, "CcuRepStoreAddImm translate failed");
    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepStoreAddImm][translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]",
            instr_id, instr_count_),
        ccu_result::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_store_add_imm::describe()
{
    return asc::format_ccu_message(
        "Store Add Imm, dst[%u] dstOffset[%u] src[%u] immAddValue[%u]", dst_.id(), dst_offset_.id(), src_.id(),
        imm_add_value_);
}

}; // namespace ccu_rep
}; // namespace asc
