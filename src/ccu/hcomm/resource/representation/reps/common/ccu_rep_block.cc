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

ccu_rep_block::ccu_rep_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label)
    : ins_generator_ptr_(ins_gen_ptr), label_(label)
{
    type_ = ccu_rep_type::block;
    instr_count_ = 0;
}

std::vector<std::shared_ptr<ccu_rep_base>>& ccu_rep_block::get_reps() { return rep_vec_; }

void ccu_rep_block::append(std::shared_ptr<ccu_rep_base> rep) { rep_vec_.push_back(rep); }

const std::string& ccu_rep_block::get_label() const { return label_; }

uint16_t ccu_rep_block::instr_count()
{
    instr_count_ = 0;
    for (const auto& rep_in_block : rep_vec_) {
        instr_count_ += rep_in_block->instr_count();
    }
    return instr_count_;
}

bool ccu_rep_block::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    constexpr uint16_t number_two = 2; // 暂定repBlock中的rep遍历2次，后续优化
    for (uint16_t i = 0; i < number_two; i++) {
        for (const auto& rep_in_block : get_reps()) {
            if (!rep_in_block->translated()) {
                rep_in_block->translate(ccu_kernel, instr, instr_id, dep);
            }
        }
    }

    return translated_;
}

std::string ccu_rep_block::describe() { return asc::format_ccu_message("RepBlock"); }

// 在块里的子 REP 中按指令编号找对应的那条；ResolveRep 往 FUNC_BLOCK 壳里
// 一层层找就是靠它；块为空或编号不在任何子 REP 范围内时返回 nullptr
std::shared_ptr<ccu_rep_base> ccu_rep_block::get_rep_by_instr_id(uint16_t instr_id)
{
    for (const auto& rep : get_reps()) {
        const uint16_t rep_count = rep->instr_count();
        if (rep_count == 0) {
            continue;
        }
        const uint16_t start_id = rep->start_instr_id();
        const uint16_t end_id = start_id + rep_count - 1;
        // 编号落在子 REP 的指令范围内，就是它
        if (instr_id >= start_id && instr_id <= end_id) {
            return rep;
        }
    }
    return nullptr;
}

}; // namespace ccu_rep
}; // namespace asc
