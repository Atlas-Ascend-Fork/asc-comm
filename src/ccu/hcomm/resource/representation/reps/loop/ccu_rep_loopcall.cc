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

ccu_rep_loop_call::ccu_rep_loop_call(ccu_ins_generater_base* ins_generator_ptr, const std::string& label)
    : ins_generator_ptr_(ins_generator_ptr), label_(label)
{
    type_ = ccu_rep_type::loop_call;
}

const std::string& ccu_rep_loop_call::get_label() const { return label_; }

void ccu_rep_loop_call::reference(std::shared_ptr<ccu_rep_loop_block> ref_rep) { loop_block_ = ref_rep; }

void ccu_rep_loop_call::set_in_arg(const variable& var)
{
    in_arg_count_++;
    in_arg_instr_count_++;
    in_args_.push_back(ccu_rep_arg(var));
}

void ccu_rep_loop_call::set_in_arg(const std::vector<variable>& var_list)
{
    in_arg_count_ += var_list.size();
    in_arg_instr_count_ += var_list.size();
    in_args_.push_back(ccu_rep_arg(var_list));
}

void ccu_rep_loop_call::set_in_arg(const memory& mem)
{
    in_arg_count_++;
    in_arg_instr_count_ += 2; // 传递Memory需要2条指令
    in_args_.push_back(ccu_rep_arg(mem));
}

void ccu_rep_loop_call::set_in_arg(const std::vector<memory>& mem_list)
{
    in_arg_count_ += mem_list.size();
    in_arg_instr_count_ += mem_list.size() * 2; // 传递Memory需要2条指令
    in_args_.push_back(ccu_rep_arg(mem_list));
}

/* 【新增】 */
void ccu_rep_loop_call::set_in_arg(const local_addr& addr)
{
    in_arg_count_++;
    in_arg_instr_count_ += 2; // 传递LocalAddr需要2条指令
    in_args_.push_back(ccu_rep_arg(addr));
}

void ccu_rep_loop_call::set_in_arg(const std::vector<local_addr>& addr_list)
{
    in_arg_count_ += addr_list.size();
    in_arg_instr_count_ += addr_list.size() * 2; // 传递LocalAddr需要2条指令
    in_args_.push_back(ccu_rep_arg(addr_list));
}

void ccu_rep_loop_call::set_in_arg(const remote_addr& addr)
{
    in_arg_count_++;
    in_arg_instr_count_ += 2; // 传递RemoteAddr需要2条指令
    in_args_.push_back(ccu_rep_arg(addr));
}

void ccu_rep_loop_call::set_in_arg(const std::vector<remote_addr>& addr_list)
{
    in_arg_count_ += addr_list.size();
    in_arg_instr_count_ += addr_list.size() * 2; // 传递RemoteAddr需要2条指令
    in_args_.push_back(ccu_rep_arg(addr_list));
}

uint16_t ccu_rep_loop_call::instr_count()
{
    instr_count_ = in_arg_instr_count_;
    return instr_count_;
}

bool ccu_rep_loop_call::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    asc::check_ccu_not_null(loop_block_, "[CcuRepLoopCall::translate] LoopBlock is nullptr!");

    if (!loop_block_->translated()) {
        asc::throw_ccu_internal("Reference To Invalid LoopBlock");
    }

    instr_id += instr_count();

    return translated_;
}

std::string ccu_rep_loop_call::describe() { return asc::format_ccu_message("LoopCall[%s]", label_.c_str()); }

}; // namespace ccu_rep
}; // namespace asc
