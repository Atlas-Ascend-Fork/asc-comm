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

namespace asc {
namespace ccu_rep {

ccu_rep_loop_block::ccu_rep_loop_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label)
    : ccu_rep_block(ins_gen_ptr, label)
{
    type_ = ccu_rep_type::loop_block;
}

std::string ccu_rep_loop_block::describe()
{
    HCCL_INFO("Begin describe LoopBlock[%s]", get_label().c_str());
    for (const auto& rep : get_reps()) {
        HCCL_INFO(" Rep: %s", rep->describe().c_str());
    }
    return asc::format_ccu_message("LoopBlock[%s]", get_label().c_str());
}

void ccu_rep_loop_block::define_arg(variable var)
{
    args_.push_back(ccu_rep_arg(var));
    HCCL_INFO("Define Arg: Index[%u], Type[Variable], Id[%u]", args_.size(), var.id());
}

void ccu_rep_loop_block::define_arg(memory mem)
{
    args_.push_back(ccu_rep_arg(mem));
    HCCL_INFO("Define Arg: Index[%u], Type[Memory], Id[%u]", args_.size(), mem.addr.id());
}

void ccu_rep_loop_block::define_arg(local_addr addr)
{
    args_.push_back(ccu_rep_arg(addr));
    HCCL_INFO("Define Arg: Index[%u], Type[LocalAddr], Id[%u]", args_.size(), addr.addr.id());
}

void ccu_rep_loop_block::define_arg(remote_addr addr)
{
    args_.push_back(ccu_rep_arg(addr));
    HCCL_INFO("Define Arg: Index[%u], Type[RemoteAddr], Id[%u]", args_.size(), addr.addr.id());
}

void ccu_rep_loop_block::define_arg(const std::vector<variable> var_list)
{
    args_.push_back(ccu_rep_arg(var_list));
    HCCL_INFO("Define Arg: Index[%u], Type[Variable List]: ", args_.size());
    for (uint32_t index = 0; index < var_list.size(); index++) {
        HCCL_INFO("    Index[%u].Id[%u]", index, var_list[index].id());
    }
}

void ccu_rep_loop_block::define_arg(const std::vector<local_addr> addr_list)
{
    args_.push_back(ccu_rep_arg(addr_list));
    HCCL_INFO("Define Arg: Index[%u], Type[LocalAddr List]: ", args_.size());
    for (uint32_t index = 0; index < addr_list.size(); index++) {
        HCCL_INFO("Index[%u].Id[%u]", index, addr_list[index].addr.id());
    }
}

void ccu_rep_loop_block::define_arg(const std::vector<remote_addr> addr_list)
{
    args_.push_back(ccu_rep_arg(addr_list));
    HCCL_INFO("Define Arg: Index[%u], Type[RemoteAddr List]: ", args_.size());
    for (uint32_t index = 0; index < addr_list.size(); index++) {
        HCCL_INFO("Index[%u].Id[%u]", index, addr_list[index].addr.id());
    }
}

void ccu_rep_loop_block::define_arg(const std::vector<memory> mem_list)
{
    args_.push_back(ccu_rep_arg(mem_list));
    HCCL_INFO("Define Arg: Index[%u], Type[Memory List]: ", args_.size());
    for (uint32_t index = 0; index < mem_list.size(); index++) {
        HCCL_INFO("Index[%u].Id[%u]", index, mem_list[index].addr.id());
    }
}

ccu_rep_arg& ccu_rep_loop_block::get_arg(uint16_t index)
{
    if (index >= args_.size()) {
        asc::throw_ccu_internal("CcuLoopBlock Arg Index[%u] Out of Range", index);
    }
    return args_[index];
}

}; // namespace ccu_rep
}; // namespace asc
