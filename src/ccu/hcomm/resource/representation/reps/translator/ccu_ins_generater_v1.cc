/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/channel/ccu_channel.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "hcomm/common/ccu_log.h"
#include "hcomm/hcomm_ccu_res.h"

namespace asc {
namespace ccu_rep {

#define UNUSED(x) (void)(x)

namespace {
template <typename t>
void load_addr_arg(ccu_instr*& instr, const t& dst, const t& src, const trans_dep& dep)
{
    load_gsagsa_instr(instr++, dst.addr.id(), src.addr.id(), dep.reserve_gsa_id);
    load_xx_instr(instr++, dst.token.id(), src.token.id(), dep.reserve_xn_id);
}

template <typename t>
HcclResult load_addr_list_arg(
    ccu_instr*& instr, const std::vector<t>& dst, const std::vector<t>& src, const trans_dep& dep)
{
    if (src.size() != dst.size()) {
        HCCL_ERROR("Mismatched Arg Size: srcSize[%u], dstSize[%u]", src.size(), dst.size());
        return HCCL_E_PARA;
    }
    for (uint32_t j = 0; j < src.size(); j++) {
        load_addr_arg(instr, dst[j], src[j], dep);
    }
    return HcclResult::HCCL_SUCCESS;
}
} // namespace

HcclResult ccu_ins_generater_v1::ccu_rep_buf_loc_read_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_read* rep_buf_loc_read, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(rep_buf_loc_read);
    trans_loc_mem_to_loc_ms_instr(
        instr++, rep_buf_loc_read->get_dst().id(), rep_buf_loc_read->get_src().addr.id(),
        rep_buf_loc_read->get_src().token.id(), rep_buf_loc_read->get_len().id(), dep.reserve_channal_id[0],
        rep_buf_loc_read->get_sem().id(), rep_buf_loc_read->get_mask(), 0, 0, 1, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_buf_loc_write_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_write* rep_buf_loc_write, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(rep_buf_loc_write);
    trans_loc_ms_to_loc_mem_instr(
        instr++, rep_buf_loc_write->get_dst().addr.id(), rep_buf_loc_write->get_dst().token.id(),
        rep_buf_loc_write->get_src().id(), rep_buf_loc_write->get_len().id(), dep.reserve_channal_id[0],
        rep_buf_loc_write->get_sem().id(), rep_buf_loc_write->get_mask(), 0, 0, 1, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_buf_read_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_read* rep_buf_read, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(dep);
    CHK_PTR_NULL(rep_buf_read);
    ccu_channel channel_impl(rep_buf_read->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", rep_buf_read->type()), HCCL_E_INTERNAL);

    trans_rmt_mem_to_loc_ms_instr(
        instr++, rep_buf_read->get_dst().id(), rep_buf_read->get_src().addr.id(), rep_buf_read->get_src().token.id(),
        rep_buf_read->get_len().id(), channel_impl->get_channel_id(), rep_buf_read->get_sem().id(),
        rep_buf_read->get_mask(), 0, 0, 1, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_write_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_write* rep_write)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(rep_write);
    ccu_channel channel_impl(rep_write->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", rep_write->type()), HCCL_E_INTERNAL);
    trans_loc_mem_to_rmt_mem_instr(
        instr++, rep_write->get_rem().addr.id(), rep_write->get_rem().token.id(), rep_write->get_loc().addr.id(),
        rep_write->get_loc().token.id(), rep_write->get_len().id(), channel_impl->get_channel_id(),
        rep_write->get_data_type(), rep_write->get_op_type(), rep_write->get_sem().id(), rep_write->get_mask(), 0, 0, 1,
        1, rep_write->get_reduce_flag());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_read_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_read* rep_read)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(rep_read);
    ccu_channel channel_impl(rep_read->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", rep_read->type()), HCCL_E_INTERNAL);
    trans_rmt_mem_to_loc_mem_instr(
        instr++, rep_read->get_loc().addr.id(), rep_read->get_loc().token.id(), rep_read->get_rem().addr.id(),
        rep_read->get_rem().token.id(), rep_read->get_len().id(), channel_impl->get_channel_id(),
        rep_read->get_data_type(), rep_read->get_op_type(), rep_read->get_sem().id(), rep_read->get_mask(), 0, 0, 1, 1,
        rep_read->get_reduce_flag());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_rem_mem_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_mem* rep_rem_mem)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(rep_rem_mem);
    ccu_channel channel_impl(rep_rem_mem->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", rep_rem_mem->type()), HCCL_E_INTERNAL);
    uint32_t size{0};
    uint32_t token_id{0};
    uint32_t token_value{0};
    uint64_t addr{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(addr, size, token_id, token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[CcuRepRemMem][%s] failed to get remote buffer, channelHandle[0x%llx].", __func__,
            rep_rem_mem->get_channel()),
        HCCL_E_UNAVAIL); // 当前认为channel只持有一个buffer

    auto token_info = get_token(token_id, token_value, 1);

    load_imd_to_gsa_instr(instr++, rep_rem_mem->get_rem().addr.id(), addr);
    load_imd_to_xn_instr(instr++, rep_rem_mem->get_rem().token.id(), token_info, ccu_load_to_xn_sec_info);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loc_cpy_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_cpy* ccu_rep_loc_cpy, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_loc_cpy);
    if (ccu_rep_loc_cpy->get_reduce_flag() == 0) {
        trans_loc_mem_to_loc_mem_instr(
            instr++, ccu_rep_loc_cpy->get_dst().addr.id(), ccu_rep_loc_cpy->get_dst().token.id(),
            ccu_rep_loc_cpy->get_src().addr.id(), ccu_rep_loc_cpy->get_src().token.id(),
            ccu_rep_loc_cpy->get_len().id(), dep.reserve_channal_id[0], ccu_rep_loc_cpy->get_sem().id(),
            ccu_rep_loc_cpy->get_mask(), 0, 0, 1, 1);
    } else {
        // 这个翻译需要验证
        trans_loc_mem_to_rmt_mem_instr(
            instr++, ccu_rep_loc_cpy->get_dst().addr.id(), ccu_rep_loc_cpy->get_dst().token.id(),
            ccu_rep_loc_cpy->get_src().addr.id(), ccu_rep_loc_cpy->get_src().token.id(),
            ccu_rep_loc_cpy->get_len().id(), dep.reserve_channal_id[0], ccu_rep_loc_cpy->get_data_type(),
            ccu_rep_loc_cpy->get_op_type(), ccu_rep_loc_cpy->get_sem().id(), ccu_rep_loc_cpy->get_mask(), 0, 0, 1, 1,
            ccu_rep_loc_cpy->get_reduce_flag());
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_buf_write_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_write* ccu_rep_buf_write, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(dep);
    CHK_PTR_NULL(ccu_rep_buf_write);
    ccu_channel channel_impl(ccu_rep_buf_write->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", ccu_rep_buf_write->type()), HCCL_E_INTERNAL);
    trans_loc_ms_to_rmt_mem_instr(
        instr++, ccu_rep_buf_write->get_dst().addr.id(), ccu_rep_buf_write->get_dst().token.id(),
        ccu_rep_buf_write->get_src().id(), ccu_rep_buf_write->get_len().id(), channel_impl->get_channel_id(),
        ccu_rep_buf_write->get_sem().id(), ccu_rep_buf_write->get_mask(), 0, 0, 1, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_buf_reduce_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_reduce* ccu_rep_buf_reduce)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_buf_reduce);
    if (ccu_rep_buf_reduce->get_count() < ccu_reduce_min_ms) {
        HCCL_ERROR("count[%u] must be at least %u", ccu_rep_buf_reduce->get_count(), ccu_reduce_min_ms);
        return HCCL_E_PARA;
    }
    if (ccu_rep_buf_reduce->get_count() > ccu_reduce_max_ms ||
        ccu_rep_buf_reduce->get_mem().size() > ccu_reduce_max_ms) {
        HCCL_ERROR(
            "count[%u] and mem size[%zu] must less than %u", ccu_rep_buf_reduce->get_count(),
            ccu_rep_buf_reduce->get_mem().size(), ccu_reduce_max_ms);
        return HCCL_E_PARA;
    }

    // 这里需要注意，在数据格式膨胀的情况下，需要传入用来存放输出的MSId
    // 特别是2P场景，输入MS的数目为2，但是在8bit进，32bit出的场景，输出MS的数目为4
    // 传入的MS中已经包含了需要使用的输入输出的最大量，因此，这里应该直接去MS的size
    auto mem = ccu_rep_buf_reduce->get_mem();
    uint16_t ms_id[ccu_reduce_max_ms] = {0};
    for (uint16_t i = 0; i < mem.size(); i++) {
        ms_id[i] = mem[i].id();
    }

    if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_sum) {
        if (ccu_rep_buf_reduce->get_output_data_type() == 1) { // 1是fp16
            add_instr(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_output_data_type(),
                ccu_rep_buf_reduce->get_data_type(), ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
                0, 0, 1, ccu_rep_buf_reduce->get_xn_id_length().id());
        } else if (ccu_rep_buf_reduce->get_output_data_type() == 2) { // 2是bf16
            add_instr(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_output_data_type(),
                ccu_rep_buf_reduce->get_data_type(), ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
                0, 0, 1, ccu_rep_buf_reduce->get_xn_id_length().id());
        } else {
            add_instr(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), 0, ccu_rep_buf_reduce->get_data_type(),
                ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(), 0, 0, 1,
                ccu_rep_buf_reduce->get_xn_id_length().id());
        }
    } else if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_max) {
        max_instr(
            instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_data_type(),
            ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(), 0, 0, 1,
            ccu_rep_buf_reduce->get_xn_id_length().id());
    } else if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_min) {
        min_instr(
            instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_data_type(),
            ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(), 0, 0, 1,
            ccu_rep_buf_reduce->get_xn_id_length().id());
    }

    return HcclResult::HCCL_SUCCESS;
}

uint32_t ccu_ins_generater_v1::get_instr_count(ccu_rep_type rep_type)
{
    if (rep_type_instr_count_.find(rep_type) == rep_type_instr_count_.end()) {
        asc::throw_ccu_internal("[%s] Unsupported repType[%d]", __func__, rep_type);
    }
    return rep_type_instr_count_[rep_type];
}

HcclResult ccu_ins_generater_v1::ccu_rep_loc_record_event_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_record_event* ccu_rep_loc_record_event)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_loc_record_event);
    set_cke_instr(instr++, ccu_rep_loc_record_event->get_event().id(), ccu_rep_loc_record_event->get_mask(), 0, 0, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loc_wait_event_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_event* ccu_rep_loc_wait_event)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_loc_wait_event);
    // SetCKEInstr支持硬件profiling功能
    if (ccu_rep_loc_wait_event->get_is_profiling()) {
        set_cke_instr(instr++, 0, 0, ccu_rep_loc_wait_event->get_event().id(), ccu_rep_loc_wait_event->get_mask(), 1);
    } else {
        clear_cke_instr(instr++, 0, 0, ccu_rep_loc_wait_event->get_event().id(), ccu_rep_loc_wait_event->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loc_wait_notify_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_notify* ccu_rep_loc_wait_notify)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_loc_wait_notify);
    // SetCKEInstr支持硬件profiling功能
    if (ccu_rep_loc_wait_notify->get_is_profiling()) {
        set_cke_instr(
            instr++, 0, 0, ccu_rep_loc_wait_notify->get_notify().id(), ccu_rep_loc_wait_notify->get_mask(), 1);
    } else {
        clear_cke_instr(
            instr++, 0, 0, ccu_rep_loc_wait_notify->get_notify().id(), ccu_rep_loc_wait_notify->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_rem_wait_sem_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_wait_sem* ccu_rep_rem_wait_sem)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_rem_wait_sem);
    ccu_channel channel_impl(ccu_rep_rem_wait_sem->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", ccu_rep_rem_wait_sem->type()), HCCL_E_INTERNAL);
    uint32_t loc_cke_id{0};
    CHK_PRT_RET(
        channel_impl->get_loc_cke_by_index(ccu_rep_rem_wait_sem->get_sem_index(), loc_cke_id) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[CcuRepRemWaitSem][%s] failed to get loc cke id, channelHandle[0x%llx], semIndex[%u].", __func__,
            ccu_rep_rem_wait_sem->get_channel(), ccu_rep_rem_wait_sem->get_sem_index()),
        HCCL_E_UNAVAIL);

    // 需要profiling的使用SetCKEInstr, 否则使用ClearCKEInstr
    if (ccu_rep_rem_wait_sem->get_is_profiling()) {
        set_cke_instr(instr++, 0, 0, loc_cke_id, ccu_rep_rem_wait_sem->get_mask(), 1);
    } else {
        clear_cke_instr(instr++, 0, 0, loc_cke_id, ccu_rep_rem_wait_sem->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_rem_post_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_var* ccu_rep_rem_post_var)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_rem_post_var);
    ccu_channel channel_impl(ccu_rep_rem_post_var->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", ccu_rep_rem_post_var->type()), HCCL_E_INTERNAL);
    uint32_t rmt_xn_id{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_xn_by_index(ccu_rep_rem_post_var->get_param_index(), rmt_xn_id) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[CcuRepRemPostSem][%s] failed to get remote xn id, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_var->get_channel()),
        HCCL_E_UNAVAIL);

    uint32_t rmt_cke_id{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_by_index(ccu_rep_rem_post_var->get_sem_index(), rmt_cke_id) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[CcuRepRemPostSem][%s] failed to get remote cke id, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_var->get_channel()),
        HCCL_E_UNAVAIL);

    sync_xn_instr(
        instr++, rmt_xn_id, ccu_rep_rem_post_var->get_param().id(), channel_impl->get_channel_id(), rmt_cke_id,
        ccu_rep_rem_post_var->get_mask(), 0, 0, 0, 0, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_rem_post_sem_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_sem* ccu_rep_rem_post_sem, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_rem_post_sem);
    ccu_channel channel_impl(ccu_rep_rem_post_sem->get_channel());
    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, type[%d]", ccu_rep_rem_post_sem->type()), HCCL_E_INTERNAL);
    uint32_t rmt_cke_id{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_by_index(ccu_rep_rem_post_sem->get_sem_index(), rmt_cke_id) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[CcuRepRemPostSem][%s] failed to get remote cke id, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_sem->get_channel()),
        HCCL_E_UNAVAIL);

    sync_cke_instr(
        instr++, rmt_cke_id, dep.reserve_cke_id, ccu_rep_rem_post_sem->get_mask(), channel_impl->get_channel_id(), 0, 0,
        0, 0, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_record_shared_notify_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_record_shared_notify* ccu_rep_record_shared_notify,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_record_shared_notify);
    // 非本die时利用环回访问
    if (ccu_rep_record_shared_notify->get_notify().die_id() != dep.die_id) {
        sync_cke_instr(
            instr++, ccu_rep_record_shared_notify->get_notify().id(), dep.reserve_cke_id,
            ccu_rep_record_shared_notify->get_mask(), dep.reserve_channal_id[1], 0, 0, 0, 0, 1);
    } else {
        set_cke_instr(
            instr++, ccu_rep_record_shared_notify->get_notify().id(), ccu_rep_record_shared_notify->get_mask(), 0, 0,
            1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_add_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_add* ccu_rep_add, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_add);
    switch (ccu_rep_add->get_sub_type()) {
        case add_sub_type::addr_plus_var_to_addr: {
            load_gsa_xn_instr(
                instr++, ccu_rep_add->get_addr_c().id(), ccu_rep_add->get_addr_a().id(), ccu_rep_add->get_var_b().id());
            break;
        }
        case add_sub_type::addr_plus_addr_to_addr: {
            load_gsagsa_instr(
                instr++, ccu_rep_add->get_addr_c().id(), ccu_rep_add->get_addr_a().id(),
                ccu_rep_add->get_addr_b().id());
            break;
        }
        case add_sub_type::var_plus_var_to_var: {
            load_xx_instr(
                instr++, ccu_rep_add->get_var_c().id(), ccu_rep_add->get_var_a().id(), ccu_rep_add->get_var_b().id());
            break;
        }
        case add_sub_type::self_add_address: {
            load_gsa_xn_instr(
                instr++, ccu_rep_add->get_addr_a().id(), ccu_rep_add->get_addr_a().id(), ccu_rep_add->get_var_b().id());
            break;
        }
        case add_sub_type::self_add_variable: {
            load_xx_instr(
                instr++, ccu_rep_add->get_var_a().id(), ccu_rep_add->get_var_a().id(), ccu_rep_add->get_var_b().id());
            break;
        }
        default: {
            HCCL_ERROR("Invalid Add, subType[%d]", static_cast<int>(ccu_rep_add->get_sub_type()));
            return HCCL_E_PARA;
        }
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_assign_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_assign* ccu_rep_assign, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_assign);
    switch (ccu_rep_assign->get_sub_type()) {
        case assign_sub_type::imd_to_variable: {
            load_imd_to_xn_instr(instr++, ccu_rep_assign->get_var_a().id(), ccu_rep_assign->get_immed());
            break;
        }
        case assign_sub_type::imd_to_addr: {
            load_imd_to_gsa_instr(instr++, ccu_rep_assign->get_addr_a().id(), ccu_rep_assign->get_immed());
            break;
        }
        case assign_sub_type::var_to_addr: {
            load_gsa_xn_instr(
                instr++, ccu_rep_assign->get_addr_a().id(), dep.reserve_gsa_id, ccu_rep_assign->get_var_a().id());
            break;
        }
        case assign_sub_type::addr_to_addr: {
            load_gsagsa_instr(
                instr++, ccu_rep_assign->get_addr_b().id(), ccu_rep_assign->get_addr_a().id(), dep.reserve_gsa_id);
            break;
        }
        case assign_sub_type::var_to_var: {
            load_xx_instr(
                instr++, ccu_rep_assign->get_var_b().id(), ccu_rep_assign->get_var_a().id(), dep.reserve_xn_id);
            break;
        }
        default: {
            HCCL_ERROR("Invalid Assign, subType[%d]", static_cast<int>(ccu_rep_assign->get_sub_type()));
            return HCCL_E_PARA;
        }
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_mul_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_mul* ccu_rep_mul)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_mul);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_sub_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sub* ccu_rep_sub)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_sub);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_and_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* ccu_rep_and, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_and);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_not_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* ccu_rep_not, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_not);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_or_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* ccu_rep_or, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_or);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_xor_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* ccu_rep_xor, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_xor);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_sh_l_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* ccu_rep_sh_l, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_sh_l);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_sh_r_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* ccu_rep_sh_r, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(ccu_rep_sh_r);
    UNUSED(dep);
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_func_block_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_block* func_block_ptr,
    const trans_dep& dep, uint32_t step)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(func_block_ptr);
    std::vector<ccu_rep_arg>& out_args = func_block_ptr->get_out_args();
    ccu_rep_reference_manager* func_manager = func_block_ptr->get_func_manager();
    CHK_PTR_NULL(func_manager);
    if (step == 0) {
        // 函数入口为nop
        load_imd_to_xn_instr(instr++, dep.reserve_xn_id, 0); // 向记录常量0的Xn再次赋值0作为nop操作
        cur_instr_id++;
    } else if (step == 1) {
        // 处理输出的参数
        uint32_t i_out_arg = 0;
        for (uint32_t i = 0; i < out_args.size(); i++) {
            if (out_args[i].type == ccu_arg_type::variable) {
                load_xx_instr(
                    instr++, func_manager->get_func_out()[i_out_arg++].id(), out_args[i].var.id(), dep.reserve_xn_id);
                cur_instr_id++;
            } else if (out_args[i].type == ccu_arg_type::variable_list) {
                for (uint32_t j = 0; j < out_args[i].var_list.size(); j++) {
                    load_xx_instr(
                        instr++, func_manager->get_func_out()[i_out_arg++].id(), out_args[i].var_list[j].id(),
                        dep.reserve_xn_id);
                    cur_instr_id++;
                }
            }
        }

        // 返回调用处
        jump_instr(instr++, func_manager->get_func_ret(func_block_ptr->get_call_layer()).id(), dep.reserve_xn_id, 1);
        cur_instr_id++;
    } else {
        HCCL_ERROR("Unsupported step[%d] for ccu_rep_func_block_translate", step);
        return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

void ccu_ins_generater_v1::load_func_call_in_args(
    ccu_instr* instr, std::vector<ccu_rep_arg>& in_args, std::vector<variable>& formal_ins,
    uint16_t reserve_xn_id) const
{
    uint32_t idx = 0;
    for (uint32_t i = 0; i < in_args.size(); i++) {
        if (in_args[i].type == ccu_arg_type::variable) {
            load_xx_instr(instr + idx, formal_ins[idx].id(), in_args[i].var.id(), reserve_xn_id);
            idx++;
        } else if (in_args[i].type == ccu_arg_type::variable_list) {
            for (uint32_t j = 0; j < in_args[i].var_list.size(); j++) {
                load_xx_instr(instr + idx, formal_ins[idx].id(), in_args[i].var_list[j].id(), reserve_xn_id);
                idx++;
            }
        }
    }
}

void ccu_ins_generater_v1::load_func_call_out_args(
    ccu_instr* instr, uint32_t offset, std::vector<ccu_rep_arg>& out_args, ccu_rep_reference_manager* func_manager,
    uint16_t reserve_xn_id)
{
    uint32_t idx = 0;
    for (uint32_t i = 0; i < out_args.size(); i++) {
        if (out_args[i].type == ccu_arg_type::variable) {
            load_xx_instr(
                instr + offset + idx, out_args[i].var.id(), func_manager->get_func_out()[idx].id(), reserve_xn_id);
            idx++;
        } else if (out_args[i].type == ccu_arg_type::variable_list) {
            for (uint32_t j = 0; j < out_args[i].var_list.size(); j++) {
                load_xx_instr(
                    instr + offset + idx, out_args[i].var_list[j].id(), func_manager->get_func_out()[idx].id(),
                    reserve_xn_id);
                idx++;
            }
        }
    }
}

HcclResult ccu_ins_generater_v1::ccu_rep_func_call_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& cur_instr_id, ccu_rep_func_call* func_call_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    (void)cur_instr;

    func_call_context ctx;
    CHK_RET(prepare_func_call_context(func_call_ptr, ctx));

    std::vector<ccu_rep_arg>& out_args = func_call_ptr->get_out_args();
    std::vector<ccu_rep_arg>& in_args = func_call_ptr->get_in_args();
    uint32_t in_arg_count = ctx.in_arg_count;
    ccu_instr* instr = ctx.instr;
    ccu_rep_reference_manager* func_manager = ctx.func_manager;
    std::vector<variable>& formal_ins = ctx.formal_ins;
    std::shared_ptr<ccu_rep_func_block>& func_block = ctx.func_block;
    load_func_call_in_args(instr, in_args, formal_ins, dep.reserve_xn_id);

    uint32_t loc_id = 0;
    if (func_block != nullptr) {
        load_imd_to_xn_instr(
            instr + in_arg_count + loc_id++, func_manager->get_func_call().id(), func_block->start_instr_id());
    } else {
        load_xx_instr(
            instr + in_arg_count + loc_id++, func_manager->get_func_call().id(),
            func_call_ptr->get_func_addr_var().id(), dep.reserve_xn_id);
    }

    load_imd_to_xn_instr(
        instr + in_arg_count + loc_id++, func_manager->get_func_ret(func_call_ptr->get_call_layer()).id(),
        func_call_ptr->start_instr_id() + in_arg_count + 3); // 需要指向函数返回位置，为输入指令Id + 3
    jump_instr(instr + in_arg_count + loc_id++, func_manager->get_func_call().id(), dep.reserve_xn_id, 1);
    load_imd_to_xn_instr(instr + in_arg_count + loc_id++, dep.reserve_xn_id, 0);

    uint32_t extra_instr_num = get_instr_count(func_call_ptr->type());
    load_func_call_out_args(instr, in_arg_count + extra_instr_num, out_args, func_manager, dep.reserve_xn_id);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump* jump_ptr, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)instr;
    (void)cur_instr_id;

    // 翻译直接跳转指令
    CHK_PTR_NULL(jump_ptr);
    std::shared_ptr<ccu_rep_jump_label> jump_label = jump_ptr->get_jump_label();
    CHK_PTR_NULL(jump_label);
    load_imd_to_xn_instr(jump_ptr->get_instr() + 0, jump_ptr->get_target_instr_id().id(), jump_label->start_instr_id());
    jump_instr(jump_ptr->get_instr() + 1, jump_ptr->get_target_instr_id().id(), dep.reserve_xn_id, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_ne_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ne* jump_ne_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(cur_instr_id);
    CHK_PTR_NULL(jump_ne_ptr);
    std::shared_ptr<ccu_rep_jump_label> jump_label = jump_ne_ptr->get_jump_label();
    CHK_PTR_NULL(jump_label);
    load_imd_to_xn_instr(
        jump_ne_ptr->get_instr() + 0, jump_ne_ptr->get_target_instr_id().id(), jump_label->start_instr_id());
    jump_instr(
        jump_ne_ptr->get_instr() + 1, jump_ne_ptr->get_target_instr_id().id(), jump_ne_ptr->get_condition().id(),
        jump_ne_ptr->get_expected_num());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_eq_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_eq* jump_eq_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(instr);
    UNUSED(cur_instr_id);

    CHK_PTR_NULL(jump_eq_ptr);
    std::shared_ptr<ccu_rep_jump_label> jump_label = jump_eq_ptr->get_jump_label();
    CHK_PTR_NULL(jump_label);
    uint32_t local_instr_index = 0;
    ccu_instr* start_instr = jump_eq_ptr->get_instr();
    variable& target_instr_id = jump_eq_ptr->get_target_instr_id();
    load_imd_to_xn_instr(
        start_instr + local_instr_index++, target_instr_id.id(),
        jump_eq_ptr->start_instr_id() + 4); // 需要指向NOP位置，为输入指令Id + 4
    jump_instr(
        start_instr + local_instr_index++, target_instr_id.id(), jump_eq_ptr->get_condition().id(),
        jump_eq_ptr->get_expected_num());
    load_imd_to_xn_instr(start_instr + local_instr_index++, target_instr_id.id(), jump_label->start_instr_id());
    jump_instr(start_instr + local_instr_index++, target_instr_id.id(), dep.reserve_xn_id, 1);
    load_imd_to_xn_instr(start_instr + local_instr_index++, dep.reserve_xn_id, 0);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_le_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_le* jump_le_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)instr;
    (void)cur_instr_id;
    CHK_PTR_NULL(jump_le_ptr);
    HCCL_ERROR("Unsupported Jump type for CcuV1: %s", jump_le_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_ge_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ge* jump_ge_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)instr;
    (void)cur_instr_id;
    CHK_PTR_NULL(jump_ge_ptr);
    HCCL_ERROR("Unsupported Jump type for CcuV1: %s", jump_ge_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_gt_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_gt* jump_gt_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)instr;
    (void)cur_instr_id;
    CHK_PTR_NULL(jump_gt_ptr);
    HCCL_ERROR("Unsupported Jump type for CcuV1: %s", jump_gt_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_jump_lt_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_lt* jump_lt_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)instr;
    (void)cur_instr_id;
    CHK_PTR_NULL(jump_lt_ptr);
    HCCL_ERROR("Unsupported Jump type for CcuV1: %s", jump_lt_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loop_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop* loop_ptr)
{
    UNUSED(ccu_kernel);
    UNUSED(cur_instr_id);
    CHK_PTR_NULL(loop_ptr);
    auto loop_block = loop_ptr->get_loop_block();
    CHK_PTR_NULL(loop_block);

    loop_instr(
        instr++, loop_block->start_instr_id(), loop_block->start_instr_id() + loop_block->instr_count() - 1,
        loop_ptr->get_loop_param()->id());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::load_loop_call_arg(
    ccu_instr*& instr, const ccu_rep_arg& in_arg, const ccu_rep_arg& blk_arg, const trans_dep& dep) const
{
    switch (in_arg.type) {
        case ccu_arg_type::variable:
            load_xx_instr(instr++, blk_arg.var.id(), in_arg.var.id(), dep.reserve_xn_id);
            break;
        case ccu_arg_type::variable_list:
            if (in_arg.var_list.size() != blk_arg.var_list.size()) {
                HCCL_ERROR(
                    "Mismatched Arg Size, inArg.varList.size[%zu], blkArg.varList.size[%zu]", in_arg.var_list.size(),
                    blk_arg.var_list.size());
                return HCCL_E_PARA;
            }
            for (uint32_t j = 0; j < in_arg.var_list.size(); j++) {
                load_xx_instr(instr++, blk_arg.var_list[j].id(), in_arg.var_list[j].id(), dep.reserve_xn_id);
            }
            break;
        case ccu_arg_type::memory:
            load_addr_arg(instr, blk_arg.mem, in_arg.mem, dep);
            break;
        case ccu_arg_type::local_addr:
            load_addr_arg(instr, blk_arg.local_addr_value, in_arg.local_addr_value, dep);
            break;
        case ccu_arg_type::remote_addr:
            load_addr_arg(instr, blk_arg.remote_addr_value, in_arg.remote_addr_value, dep);
            break;
        case ccu_arg_type::memory_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.mem_list, in_arg.mem_list, dep));
            break;
        case ccu_arg_type::local_addr_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.local_addr_list, in_arg.local_addr_list, dep));
            break;
        case ccu_arg_type::remote_addr_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.remote_addr_list, in_arg.remote_addr_list, dep));
            break;
        default:
            HCCL_ERROR("Mismatched Arg Type, inArg.type[%d]", static_cast<int>(in_arg.type));
            return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loop_call_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_call* loop_call_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    UNUSED(cur_instr_id);
    CHK_PTR_NULL(loop_call_ptr);
    auto loop_block = loop_call_ptr->get_loop_block();
    CHK_PTR_NULL(loop_block);
    std::vector<ccu_rep_arg>& in_args = loop_call_ptr->get_in_args();

    for (uint32_t i = 0; i < in_args.size(); i++) {
        const ccu_rep_arg& blk_arg = loop_block->get_arg(i);
        const ccu_rep_arg& in_arg = in_args[i];
        if (in_arg.type != blk_arg.type) {
            HCCL_ERROR(
                "Mismatched Arg Type, inArg.type[%d], blkArg.type[%d]", static_cast<int>(in_arg.type),
                static_cast<int>(blk_arg.type));
            return HCCL_E_PARA;
        }
        CHK_RET(load_loop_call_arg(instr, in_arg, blk_arg, dep));
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_set_loop_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_set_loop* set_loop_ptr)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(set_loop_ptr);
    load_imd_to_xn_instr(
        instr++, set_loop_ptr->loop_param.id(), get_loop_param(set_loop_ptr->executor_value.id(), 0, 0));
    load_xx_instr(instr++, set_loop_ptr->loop_param.id(), set_loop_ptr->loop_param.id(), set_loop_ptr->var.id());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_load_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load* load_ptr, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(load_ptr);
    uint64_t var_addr = dep.xn_base_addr[dep.die_id] + HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_ptr->get_var().id();

    load_imd_to_gsa_instr(instr++, dep.comm_gsa[0], var_addr);
    load_imd_to_gsa_instr(instr++, dep.comm_gsa[1], load_ptr->get_addr());
    load_imd_to_xn_instr(instr++, dep.comm_xn[0], dep.ccu_res_space_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[1], dep.mem_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[2], HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_ptr->get_num());
    trans_loc_mem_to_loc_mem_instr(
        instr++, dep.comm_gsa[0], dep.comm_xn[0], dep.comm_gsa[1], dep.comm_xn[1], dep.comm_xn[2],
        dep.reserve_channal_id[0], dep.comm_signal, load_ptr->get_mask(), 0, 0, 1, 1);
    set_cke_instr(instr++, 0, 0, dep.comm_signal, load_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_load_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_var* load_var_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(load_var_ptr);
    uint64_t var_addr = dep.xn_base_addr[dep.die_id] + HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_var_ptr->get_var().id();
    load_imd_to_gsa_instr(instr++, dep.comm_gsa[0], var_addr);
    load_gsa_xn_instr(instr++, dep.comm_gsa[1], dep.reserve_gsa_id, load_var_ptr->get_src().id());
    load_imd_to_xn_instr(instr++, dep.comm_xn[0], dep.ccu_res_space_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[1], dep.mem_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[2], HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_var_ptr->get_num());
    trans_loc_mem_to_loc_mem_instr(
        instr++, dep.comm_gsa[0], dep.comm_xn[0], dep.comm_gsa[1], dep.comm_xn[1], dep.comm_xn[2],
        dep.reserve_channal_id[0], dep.comm_signal, load_var_ptr->get_mask(), 0, 0, 1, 1);
    set_cke_instr(instr++, 0, 0, dep.comm_signal, load_var_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_load_arg_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_arg* load_arg_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(load_arg_ptr);
    if (dep.is_func_block) {
        // Xn(var) = Xn(loadXnId) + 0
        load_xx_instr(instr++, load_arg_ptr->get_var().id(), dep.load_xn_id, dep.reserve_xn_id);
    } else {
        load_sqe_args_to_xn_instr(instr++, load_arg_ptr->get_var().id(), load_arg_ptr->get_arg_id());
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_nop_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_nop* nop_ptr, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    (void)nop_ptr;
    load_imd_to_xn_instr(instr++, dep.reserve_xn_id, 0);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_store_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store* store_ptr, const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(store_ptr);
    uint64_t var_addr = dep.xn_base_addr[dep.die_id] + HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_ptr->get_var().id();

    load_imd_to_gsa_instr(instr++, dep.comm_gsa[0], store_ptr->get_addr());
    load_imd_to_gsa_instr(instr++, dep.comm_gsa[1], var_addr);
    load_imd_to_xn_instr(instr++, dep.comm_xn[0], dep.mem_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[1], dep.ccu_res_space_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[2], HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_ptr->get_num());
    trans_loc_mem_to_loc_mem_instr(
        instr++, dep.comm_gsa[0], dep.comm_xn[0], dep.comm_gsa[1], dep.comm_xn[1], dep.comm_xn[2],
        dep.reserve_channal_id[0], dep.comm_signal, store_ptr->get_mask(), 0, 0, 1, 1);
    set_cke_instr(instr++, 0, 0, dep.comm_signal, store_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_store_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_var* store_var_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    (void)cur_instr_id;
    CHK_PTR_NULL(store_var_ptr);
    uint64_t var_addr = dep.xn_base_addr[dep.die_id] + HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_var_ptr->get_var().id();

    load_imd_to_gsa_instr(instr++, dep.comm_gsa[0], var_addr);
    load_gsa_xn_instr(instr++, dep.comm_gsa[1], dep.reserve_gsa_id, store_var_ptr->get_dst().id());
    load_imd_to_xn_instr(instr++, dep.comm_xn[0], dep.mem_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[1], dep.ccu_res_space_token_info, ccu_load_to_xn_sec_info);
    load_imd_to_xn_instr(instr++, dep.comm_xn[2], HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_var_ptr->get_num());
    trans_loc_mem_to_loc_mem_instr(
        instr++, dep.comm_gsa[1], dep.comm_xn[0], dep.comm_gsa[0], dep.comm_xn[1], dep.comm_xn[2],
        dep.reserve_channal_id[0], dep.comm_signal, store_var_ptr->get_mask(), 0, 0, 1, 1);
    set_cke_instr(instr++, 0, 0, dep.comm_signal, store_var_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v1::ccu_rep_loop_group_bundle_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_group_bundle* bundle_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(bundle_ptr);
    const auto& loops = bundle_ptr->get_loops();

    load_loop_group_params(instr, cur_instr_id, bundle_ptr, dep);
    load_loop_group_bundle_config(instr, cur_instr_id, bundle_ptr);

    constexpr uint16_t k_loop_entry_instr_offset = 3;
    loop_group_instr(
        instr++, cur_instr_id + k_loop_entry_instr_offset, bundle_ptr->get_parallel_var().id(),
        bundle_ptr->get_offset_param().id(),
        0); // 向后3条为loop指令
    cur_instr_id++;

    uint16_t loop_count = static_cast<uint16_t>(loops.size());
    uint16_t jump_target_instr_id = cur_instr_id + 2 + loop_count + 1; // 跳转目标为向后2条+loop指令条数+额外1条
    load_imd_to_xn_instr(instr++, dep.reserve_xn_id, jump_target_instr_id);
    cur_instr_id++;
    jump_instr(instr++, dep.reserve_xn_id, dep.reserve_xn_id, 1);
    cur_instr_id++;

    for (const auto& loop : loops) {
        const auto& block = loop.rep_loop_block;
        CHK_PTR_NULL(block);
        loop_instr(
            instr++, block->start_instr_id(), block->start_instr_id() + block->instr_count() - 1,
            loop.loop_param_var.id());
        cur_instr_id++;
    }

    load_imd_to_xn_instr(instr++, dep.reserve_xn_id, 0);
    cur_instr_id++;

    return HcclResult::HCCL_SUCCESS;
}

void ccu_ins_generater_v1::load_loop_group_params(
    ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr, const trans_dep& dep) const
{
    const auto& loops = bundle_ptr->get_loops();
    for (const auto& loop : loops) {
        if (loop.layout_value == ccu_rep_loop_group_bundle::layout::config) {
            uint64_t lp_imm = get_loop_param(loop.executor_value.id(), loop.config.addr_offset, loop.config.iter_num);
            load_imd_to_xn_instr(instr++, loop.loop_param_var.id(), lp_imm);
            cur_instr_id++;
        } else {
            uint64_t ctx_imm = static_cast<uint64_t>(loop.executor_value.id()) << 45; // 左移45位到对应字段然后相加
            load_imd_to_xn_instr(instr++, dep.reserve_xn_id, ctx_imm);
            cur_instr_id++;
            load_xx_instr(instr++, loop.loop_param_var.id(), loop.loop_param_var.id(), dep.reserve_xn_id);
            cur_instr_id++;
        }
    }
}

void ccu_ins_generater_v1::load_loop_group_bundle_config(
    ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr) const
{
    if (bundle_ptr->get_layout() != ccu_rep_loop_group_bundle::layout::config) {
        return;
    }
    uint64_t parallel_imm = get_parallel_param(
        bundle_ptr->get_config().clone_num, bundle_ptr->get_repeat_loop_idx(), bundle_ptr->get_total_loop_num());
    load_imd_to_xn_instr(instr++, bundle_ptr->get_parallel_var().id(), parallel_imm);
    cur_instr_id++;

    uint64_t offset_imm = ::asc::ccu_rep::get_offset_param(
        bundle_ptr->get_config().addr_offset, bundle_ptr->get_config().ccu_buffer_offset,
        bundle_ptr->get_config().event_offset);
    load_imd_to_xn_instr(instr++, bundle_ptr->get_offset_param().id(), offset_imm);
    cur_instr_id++;
}

uint16_t ccu_ins_generater_v1::ccu_rep_loop_group_bundle_instr_count(const ccu_rep_loop_group_bundle* bundle_ptr) const
{
    if (bundle_ptr == nullptr) {
        asc::throw_ccu_internal("[%s] bundlePtr is nullptr", __func__);
    }
    const auto& loops = bundle_ptr->get_loops();
    const uint16_t loop_count = static_cast<uint16_t>(loops.size());
    uint16_t var_based_loop_count = 0;
    for (const auto& loop : loops) {
        if (loop.layout_value != ccu_rep_loop_group_bundle::layout::config) {
            var_based_loop_count++;
        }
    }
    // 每 loop：config 1 条载入 / var 2 条；config bundle 额外 2 条(parallel+offset)；+loopgroup1 +跳过2 +每loop
    // Loop1 +收尾1
    const uint16_t group_offset = (loop_count - var_based_loop_count) + (var_based_loop_count * 2) +
                                  (bundle_ptr->get_layout() == ccu_rep_loop_group_bundle::layout::config ? 2 : 0);
    return group_offset + 1 + 2 + loop_count + 1; // 跳过2条
}

} // namespace ccu_rep
} // namespace asc
