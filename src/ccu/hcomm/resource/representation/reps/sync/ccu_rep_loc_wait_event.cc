/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_event.h"
#include <climits>
#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
namespace asc {
namespace ccu_rep {

ccu_rep_loc_wait_event::ccu_rep_loc_wait_event(
    ccu_ins_generater_base* ins_gen_ptr, const completed_event& event, uint32_t mask, bool is_profiling)
    : ins_gen_ptr_(ins_gen_ptr), event_(event), mask_(mask), is_profiling_(is_profiling)
{
    type_ = ccu_rep_type::loc_wait_event;
    instr_count_ = ins_gen_ptr->get_instr_count(type_);
}

void ccu_rep_loc_wait_event::set_dependency_info(
    const std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>>& dep_info)
{
    dep_info_ = dep_info;
}

std::vector<std::shared_ptr<ccu_rep_base>> ccu_rep_loc_wait_event::get_dependency_info(uint32_t bit)
{
    // 查找给定 bit 是否存在于 depInfo_ 中
    auto it = dep_info_.find(bit);
    // 如果找到 bit，返回与之关联的 vector
    if (it != dep_info_.end()) {
        return it->second;
    }
    // 如果未找到 bit，返回一个空的 vector
    return std::vector<std::shared_ptr<ccu_rep_base>>();
}

bool ccu_rep_loc_wait_event::translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_loc_wait_event_translate(ccu_kernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepLocWaitEvent][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepLocWaitEvent translate failed");

    CHK_PRT_THROW(
        (instr_id > UINT16_MAX - instr_count_),
        HCCL_ERROR(
            "[CcuRepLocWaitEvent::translate]uint16 integer overflow occurs, "
            "instrId = [%hu], instrCount = [%hu]",
            instr_id, instr_count_),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instr_id += instr_count_;
    return translated_;
}

std::string ccu_rep_loc_wait_event::describe()
{
    return asc::format_ccu_message("CcuRepLocWaitEvent=id[%u], mask[%04x]", event_.id(), mask_);
}

}; // namespace ccu_rep
}; // namespace asc
