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
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"

#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_func_call::ccu_rep_func_call(ccu_ins_generater_base* ins_gen_ptr, const std::string& label)
    : ins_generator_ptr_(ins_gen_ptr), label_(label)
{
    type_ = ccu_rep_type::func_call;
    instr_count_ = 0;
}

ccu_rep_func_call::ccu_rep_func_call(ccu_ins_generater_base* ins_gen_ptr, const variable& func_addr_var)
    : ins_generator_ptr_(ins_gen_ptr), label_(""), func_addr_var_(func_addr_var)
{
    type_ = ccu_rep_type::func_call;
}

const std::string& ccu_rep_func_call::get_label() const { return label_; }

void ccu_rep_func_call::reference(std::shared_ptr<ccu_rep_func_block> ref_rep) { func_block_ = ref_rep; }

void ccu_rep_func_call::set_func_manager(ccu_rep_reference_manager* func_manager)
{
    this->func_manager_ = func_manager;
}

void ccu_rep_func_call::set_in_arg(const variable& var)
{
    in_arg_count_++;
    in_args_.push_back(ccu_rep_arg(var));
}

void ccu_rep_func_call::set_out_arg(const variable& var)
{
    out_arg_count_++;
    if (out_arg_count_ > func_arg_max) {
        asc::throw_ccu_internal("CcuFunc Max ArgCount = %u", func_arg_max);
    }
    out_args_.push_back(ccu_rep_arg(var));
}

void ccu_rep_func_call::set_in_arg(const std::vector<variable>& var_list)
{
    in_arg_count_ += var_list.size();
    in_args_.push_back(ccu_rep_arg(var_list));
}

void ccu_rep_func_call::set_out_arg(const std::vector<variable>& var_list)
{
    out_arg_count_ += var_list.size();
    if (out_arg_count_ > func_arg_max) {
        asc::throw_ccu_internal("CcuFunc Max ArgCount = %u", func_arg_max);
    }
    out_args_.push_back(ccu_rep_arg(var_list));
}

uint16_t ccu_rep_func_call::instr_count()
{
    instr_count_ = in_arg_count_ + out_arg_count_ +
                   ins_generator_ptr_->get_instr_count(type_); // funcCall除去入参和出参的处理外，需要额外4条指令
    return instr_count_;
}

bool ccu_rep_func_call::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    if (func_manager_ == nullptr) {
        asc::throw_ccu_internal("funcManager is nullptr");
    }
    // 未实现, FuncCall和FuncBlock中的args个数校验

    if (this->instr_ == nullptr) {
        this->instr_id_ = instr_id;
        this->instr_ = instr;
        instr += instr_count();
        instr_id += instr_count();
    }

    if (func_block_ != nullptr && !func_block_->translated()) {
        return translated_;
    }

    translated_ = true;

    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_func_call_translate(ccu_kernel, instr, instr_id, this, dep) !=
            CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepFuncCall][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepFuncCall translate failed");

    return translated_;
}

std::string ccu_rep_func_call::describe() { return asc::format_ccu_message("FuncCall[%s]", label_.c_str()); }

int32_t ccu_rep_func_call::get_call_layer()
{
    return func_block_ == nullptr ? func_nest_max : func_block_->get_call_layer();
}

}; // namespace ccu_rep
}; // namespace asc
