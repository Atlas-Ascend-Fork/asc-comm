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
#include "hcomm/resource/representation/reps/translator/ccu_rep_translator_v1.h"

#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_func_block::ccu_rep_func_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label)
    : ccu_rep_block(ins_gen_ptr, label)
{
    type_ = ccu_rep_type::func_block;
    instr_count_ = 0;
}

std::string ccu_rep_func_block::describe() { return asc::format_ccu_message("FuncBlock[%s]", get_label().c_str()); }

void ccu_rep_func_block::set_func_manager(ccu_rep_reference_manager* func_manager)
{
    this->func_manager_ = func_manager;
}

void ccu_rep_func_block::set_call_layer(uint16_t call_layer)
{
    if (call_layer != func_call_layer_invalid) {
        this->call_layer_ = call_layer;
        return;
    }

    uint16_t inner_call_layer = 0;
    for (const auto& rep : get_reps()) {
        if (rep->type() == ccu_rep_type::func_call) {
            inner_call_layer = std::static_pointer_cast<ccu_rep_func_call>(rep)->get_call_layer() + 1;
            this->call_layer_ = this->call_layer_ > inner_call_layer ? this->call_layer_ : inner_call_layer;
        }
    }
    if (this->call_layer_ > func_nest_max - 1) {
        asc::throw_ccu_internal("Max Func Call Nest Num is %u", func_nest_max);
    }
}

uint16_t ccu_rep_func_block::get_call_layer() const { return call_layer_; }

void ccu_rep_func_block::define_in_arg(const variable& var)
{
    in_arg_count_++;
    in_args_.push_back(ccu_rep_arg(var));
    HCCL_INFO("Define Input Arg: Index[%u], Type[Variable] Id[%u]", in_args_.size(), var.id());
}

void ccu_rep_func_block::define_out_arg(const variable& var)
{
    out_arg_count_++;
    if (out_arg_count_ > func_arg_max) {
        asc::throw_ccu_internal("CcuFunc Max ArgCount = %u", func_arg_max);
    }
    out_args_.push_back(ccu_rep_arg(var));
    HCCL_INFO("Define Output Arg: Index[%u], Type[Variable] Id[%u]", out_args_.size(), var.id());
}

void ccu_rep_func_block::define_in_arg(const std::vector<variable>& var_list)
{
    in_arg_count_ += var_list.size();
    in_args_.push_back(ccu_rep_arg(var_list));
    HCCL_INFO("Define Input Arg: Index[%u], Type[Variable List]: ", in_args_.size());
    for (uint32_t index = 0; index < var_list.size(); index++) {
        HCCL_INFO("    Index[%u].Id[%u]", index, var_list[index].id());
    }
}

void ccu_rep_func_block::define_out_arg(const std::vector<variable>& var_list)
{
    out_arg_count_ += var_list.size();
    if (out_arg_count_ > func_arg_max) {
        asc::throw_ccu_internal("CcuFunc Max ArgCount = %u", func_arg_max);
    }
    out_args_.push_back(ccu_rep_arg(var_list));
    HCCL_INFO("Define Output Arg: Index[%u], Type[Variable List]: ", out_args_.size());
    for (uint32_t index = 0; index < var_list.size(); index++) {
        HCCL_INFO("    Index[%u].Id[%u]", index, var_list[index].id());
    }
}

std::vector<variable> ccu_rep_func_block::get_in_arg_vars() const
{
    std::vector<variable> vars;
    for (const auto& arg : in_args_) {
        if (arg.type == ccu_arg_type::variable) {
            vars.push_back(arg.var);
        } else if (arg.type == ccu_arg_type::variable_list) {
            vars.insert(vars.end(), arg.var_list.begin(), arg.var_list.end());
        }
    }
    return vars;
}
uint16_t ccu_rep_func_block::instr_count()
{
    instr_count_ = ccu_rep_block::instr_count() + in_arg_count_ + out_arg_count_ +
                   ins_generator_ptr_->get_instr_count(type_); // FuncBlock需要额外指令
    return instr_count_;
}

bool ccu_rep_func_block::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    if (func_manager_ == nullptr) {
        asc::throw_ccu_internal("funcManager is nullptr");
    }

    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_func_block_translate(ccu_kernel, instr, instr_id, this, dep, 0) !=
            CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepFuncBlock][translate] failed to translate inArgs processing for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepFuncBlock translate failed");
    // 使用空实现的自定义删除器，避免智能指针析构时释放对象
    auto translator = ccu_rep_translator(
        std::shared_ptr<ccu_rep_reference_manager>(func_manager_, [](ccu_rep_reference_manager* ptr) {}), dep);
    translator.translate(
        ccu_kernel, get_reps(), instr, instr_id, [](std::shared_ptr<ccu_rep_base> rep) -> bool { return true; });

    CHK_PRT_THROW(
        ins_generator_ptr_->ccu_rep_func_block_translate(ccu_kernel, instr, instr_id, this, dep, 1) !=
            CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepFuncBlock][translate] failed to translate outArgs processing for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepFuncBlock translate failed");

    return translated_;
}

}; // namespace ccu_rep
}; // namespace asc
