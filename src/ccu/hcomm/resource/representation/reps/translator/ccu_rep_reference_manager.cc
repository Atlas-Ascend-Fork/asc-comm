/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"
#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

ccu_rep_reference_manager::ccu_rep_reference_manager(uint8_t dei_id) : die_id_(dei_id)
{
    func_in_var_.resize(func_arg_max);
    func_out_var_.resize(func_arg_max);
    func_call_var_.resize(
        1 + func_nest_max +
        1); // FUNC_NEST_MAX个xn存放返回地址，1个xn存放block的起始地址和1个xn存放函数地址调用时返回地址
}

void ccu_rep_reference_manager::get_res(ccu_rep_resource& res)
{
    res.variable[die_id_].insert(res.variable[die_id_].end(), func_in_var_.begin(), func_in_var_.end());
    res.variable[die_id_].insert(res.variable[die_id_].end(), func_out_var_.begin(), func_out_var_.end());
    res.variable[die_id_].insert(res.variable[die_id_].end(), func_call_var_.begin(), func_call_var_.end());
}

bool ccu_rep_reference_manager::check_valid(const std::string& label)
{
    return (reference_map_.find(label) != reference_map_.end());
}

bool ccu_rep_reference_manager::check_unique(const std::string& label)
{
    return (reference_map_.find(label) == reference_map_.end());
}

std::shared_ptr<ccu_rep_block> ccu_rep_reference_manager::get_ref_block(const std::string& label)
{
    if (!check_valid(label)) {
        asc::throw_ccu_internal("Invalid Reference: %s", label.c_str());
    }
    return reference_map_[label];
}

void ccu_rep_reference_manager::set_ref_block(const std::string& label, std::shared_ptr<ccu_rep_block> ref_block)
{
    if (!check_unique(label)) {
        asc::throw_ccu_internal("Duplicate Definition: %s", label.c_str());
    }
    reference_map_[label] = ref_block;
}

uint16_t ccu_rep_reference_manager::get_func_addr(const std::string& label)
{
    if (!check_valid(label)) {
        asc::throw_ccu_internal("Invalid Reference: %s", label.c_str());
    }

    if (reference_map_[label]->type() != ccu_rep_type::func_block) {
        asc::throw_ccu_internal("Invalid Type, %s Must be FuncBlock", label.c_str());
    }

    return reference_map_[label]->start_instr_id();
}

const variable& ccu_rep_reference_manager::get_func_call() { return func_call_var_[0]; }

const variable& ccu_rep_reference_manager::get_func_ret(uint16_t call_layer)
{
    if (call_layer > func_nest_max) {
        asc::throw_ccu_internal("Max Func Call Nest Num is %u, callLayer = %u", func_nest_max, call_layer);
    }
    return func_call_var_[call_layer + 1];
}

const std::vector<variable>& ccu_rep_reference_manager::get_func_in() { return func_in_var_; }

const std::vector<variable>& ccu_rep_reference_manager::get_func_out() { return func_out_var_; }

void ccu_rep_reference_manager::dump() const
{
    for (const auto& kv : reference_map_) {
        HCCL_INFO("refBlock[%s]:", kv.first.c_str());
    }
}

void ccu_rep_reference_manager::clear_rep_reference() { reference_map_.clear(); }

}; // namespace ccu_rep
}; // namespace asc
