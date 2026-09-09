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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/channel/ccu_channel.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_buf_write::ccu_rep_buf_write(
    ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, ccu_buf src, remote_addr dst, variable len,
    completed_event sem, uint16_t mask)
    : ins_gen_ptr_(ins_gen_ptr), channel_(channel), src_(src), dst_(dst), len_(len), sem_(sem), mask_(mask)
{
    type_ = ccu_rep_type::buf_write;
    instr_count_ = 1;
}

bool ccu_rep_buf_write::translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep)
{
    this->instr_id_ = instr_id;
    translated_ = true;

    instr_count_ = ins_gen_ptr_->get_instr_count(type_);
    CHK_PRT_THROW(
        ins_gen_ptr_->ccu_rep_buf_write_translate(ccu_kernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepBufWrite][translate] failed to translate for instrId[%u]", instr_id),
        CcuResult::CCU_E_INTERNAL, "CcuRepBufWrite translate failed");
    instr_id += instr_count_;

    return translated_;
}

std::string ccu_rep_buf_write::describe()
{
    ccu_channel channel_impl(channel_);
    if (channel_impl.get_result() != HcclResult::HCCL_SUCCESS) {
        asc::throw_ccu_internal("failed to get ccu channel, type[%d]", type_);
    }
    return asc::format_ccu_message(
        "Write CcuBuf[%u] To Rmt Mem[%u], len[%u], ChannalId[%u], sem[%u], mask[%04x]", src_.id(), dst_.addr.id(),
        len_.id(), channel_impl->get_channel_id(), sem_.id(), mask_);
}

}; // namespace ccu_rep
}; // namespace asc
