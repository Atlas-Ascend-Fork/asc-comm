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
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_store::ccu_rep_store(ccu_ins_generater_base* ins_gen_ptr, const variable& var, uint64_t addr, uint32_t num)
    : ins_generator_ptr_(ins_gen_ptr), var_(var), addr_(addr), num_(num)
{
    type_ = ccu_rep_type::store;
    instr_count_ = ins_generator_ptr_->get_instr_count(type_);
}

bool ccu_rep_store::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;
    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_store_translate(ccu_kernel, instr, instr_id, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepStore][translate] failed to translate for instrId[%u]", instr_id), CcuResult::CCU_E_INTERNAL,
        "CcuRepStore translate failed");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_store::describe()
{
    return asc::format_ccu_message("Store([%u], [%llu], [%u])", var_.id(), addr_, num_);
}

}; // namespace ccu_rep
}; // namespace asc
