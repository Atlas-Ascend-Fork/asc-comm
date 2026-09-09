/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/loop/ccu_rep_loopgroup_bundle_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

ccu_rep_loop_group_bundle::ccu_rep_loop_group_bundle(
    ccu_ins_generater_base* ins_gen_ptr, const ccu_loop_group_config& config, const variable& parallel_var,
    const variable& offset_var)
    : ins_gen_ptr_(ins_gen_ptr), config_(config), parallel_var_(parallel_var), offset_var_(offset_var)
{
    type_ = ccu_rep_type::loop_group;
}

ccu_rep_loop_group_bundle::ccu_rep_loop_group_bundle(
    ccu_ins_generater_base* ins_gen_ptr, const variable& parallel_var, const variable& offset_var)
    : ins_gen_ptr_(ins_gen_ptr),
      config_{},
      parallel_var_(parallel_var),
      offset_var_(offset_var),
      layout_(layout::packed_var)
{
    type_ = ccu_rep_type::loop_group;
}

void ccu_rep_loop_group_bundle::add_loop(const loop_entry& entry) { loops_.push_back(entry); }

uint16_t ccu_rep_loop_group_bundle::loop_group_instr_offset_in_bundle() const
{
    uint16_t loop_count = static_cast<uint16_t>(loops_.size());
    uint16_t var_based_loop_count = 0;
    for (const auto& loop : loops_) {
        if (loop.layout_value != layout::config) {
            var_based_loop_count++;
        }
    }
    constexpr uint16_t k_var_based_loop_instr_count = 2;
    constexpr uint16_t k_config_layout_extra_instr_count = 2;
    return (loop_count - var_based_loop_count) + (var_based_loop_count * k_var_based_loop_instr_count) +
           (layout_ != layout::config ? 0 : k_config_layout_extra_instr_count);
}

uint16_t ccu_rep_loop_group_bundle::get_start_loop_instr_id() const
{
    return instr_id_ + loop_group_instr_offset_in_bundle() + 3;
}

uint16_t ccu_rep_loop_group_bundle::instr_count()
{
    instr_count_ = ins_gen_ptr_->ccu_rep_loop_group_bundle_instr_count(this);
    return instr_count_;
}

bool ccu_rep_loop_group_bundle::translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    for (const auto& loop : loops_) {
        if (!loop.rep_loop_block->translated()) {
            return false;
        }
    }

    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_loop_group_bundle_translate(ccu_kernel, instr, instr_id, this, dep) !=
            CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepLoopGroupBundle][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepLoopGroupBundle translate failed");

    return translated_;
}

std::string ccu_rep_loop_group_bundle::describe()
{
    return asc::format_ccu_message("LoopGroupBundle[loops=%zu, totalLoopNum=%lu]", loops_.size(), total_loop_num_);
}

}; // namespace ccu_rep
}; // namespace asc
