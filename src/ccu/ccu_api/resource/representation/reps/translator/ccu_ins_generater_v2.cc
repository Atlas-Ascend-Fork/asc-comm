/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "ccu_api/resource/representation/reps/translator/ccu_ins_generater_v2.h"
#include "ccu_api/resource/representation/ccu_rep_v1.h"
#include "ccu_api/resource/channel/ccu_channel.h"
#include "ccu_api/common/ccu_exception.h"
#include "ccu_api/resource/microcode/ccu_assist_v1.h"

#include <iostream>

namespace asc {

namespace ccu_rep {

constexpr int CCU_CONDITION_TYPE_DEFAULT = 6;
constexpr uint64_t CCU_V2_XN_RESERVE_SIZE = 256 * 1024; // V2 XN 预留空间 256K（对齐 hcomm 段布局）
constexpr uint64_t CCU_CKE_PER_SIZE = 8; // 单个 CKE 资源步长 // 无条件跳转（对齐 hcomm ConditionType::DEFAULT）
#define UNUSED(x) static_cast<void>(x)
    // V2中所有variable的Id()需要换成IdWithFlag()
namespace {
constexpr uint8_t URMA_DMA_OP_READ = 0x6;                // UB URMA WQEBB opcode: read
constexpr uint8_t URMA_DMA_OP_WRITE = 0x3;               // UB URMA WQEBB opcode: write
constexpr uint8_t URMA_DMA_OP_WRITE_WITH_CNT_INC = 0x1A; // UB URMA WQEBB opcode: write with cascade counter increase
constexpr uint8_t SPLIT_MODE_PACKET = 1;                 // TransMem按包为单位切分
constexpr uint16_t TRANSMEM_LEN_GUARD_SKIP_OFFSET = 6; // Jump 落点：跳过超限分支 5 条，落到正常 TransMem
constexpr uint32_t REL_JMP_INSTR_NUM = 9;              // RelJmp生成的指令数
constexpr uint32_t FUNC_CALL_JMP_OFFSET = 11;          // RelJmp+Jump+Nop
constexpr uint32_t FUNC_CALL_RET_OFFSET = 12;          // FUNC_CALL_JMP_OFFSET + 1
// loop_param_var 布局：iterNum[12:0] gsaOffset[44:13] ctxId[52:45]
constexpr uint64_t LOOP_ITER_NUM_MASK = 0x1FFFULL;
constexpr uint32_t LOOP_GSA_OFFSET_SHIFT = 13;
constexpr uint64_t LOOP_GSA_OFFSET_MASK = 0xFFFFFFFFULL;
constexpr uint64_t LOOP_FIELD_MASK = 0x7FULL;

template <typename T>
void load_addr_arg(ccu_instr*& instr, const T& dst, const T& src)
{
    ccu_v2::assign(instr++, dst.addr.id(), src.addr.id());
    ccu_v2::assign(instr++, dst.token.id(), src.token.id());
}

template <typename T>
HcclResult load_addr_list_arg(ccu_instr*& instr, const std::vector<T>& dst, const std::vector<T>& src)
{
    if (src.size() != dst.size()) {
        HCCL_ERROR("Mismatched Arg Size: srcSize[%u], dstSize[%u]", src.size(), dst.size());
        return HCCL_E_PARA;
    }
    for (uint32_t j = 0; j < src.size(); j++) {
        load_addr_arg(instr, dst[j], src[j]);
    }
    return HcclResult::HCCL_SUCCESS;
}

} // namespace

uint32_t ccu_ins_generater_v2::get_instr_count(ccu_rep_type rep_type)
{
    if (rep_type_instr_count_.find(rep_type) == rep_type_instr_count_.end()) {
        asc::throw_ccu_internal("[%s] Unsupported rep_type[%d]", __func__, rep_type);
    }
    return rep_type_instr_count_[rep_type];
}

HcclResult ccu_ins_generater_v2::ccu_rep_buf_loc_read_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_read* rep_buf_loc_read,
    const trans_dep& dep)
{
    CHK_PTR_NULL(rep_buf_loc_read);
    ccu_v2::cache_config config = {};

    ccu_v2::trans_loc_mem_to_loc_ms(
        instr++, rep_buf_loc_read->get_dst().id(), rep_buf_loc_read->get_src().addr.id(),
        rep_buf_loc_read->get_src().token.id(), rep_buf_loc_read->get_len().id(), dep.reserve_xn_id,
        rep_buf_loc_read->get_sem().id(), rep_buf_loc_read->get_mask(), config);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_buf_loc_write_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_write* rep_buf_loc_write,
    const trans_dep& dep)
{
    CHK_PTR_NULL(rep_buf_loc_write);
    ccu_v2::cache_config config = {};

    ccu_v2::trans_loc_ms_to_loc_mem(
        instr++, rep_buf_loc_write->get_dst().addr.id(), rep_buf_loc_write->get_dst().token.id(),
        rep_buf_loc_write->get_src().id(), rep_buf_loc_write->get_len().id(), dep.reserve_xn_id,
        rep_buf_loc_write->get_sem().id(), rep_buf_loc_write->get_mask(), config);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_buf_read_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_read* rep_buf_read, const trans_dep& dep)
{
    CHK_PTR_NULL(rep_buf_read);
    CHK_PTR_NULL(ccu_kernel);
    ccu_channel channel_impl(rep_buf_read->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_buf_read->get_channel()), HCCL_E_INTERNAL);

    ccu_v2::trans_mem_notify_info notify = {};
    ccu_v2::trans_mem_reduce_info reduce = {};
    ccu_v2::trans_mem_config config = {};
    config.dma_op_code = URMA_DMA_OP_READ; // UB URMA WQEBB opcode: read
    config.src_mode = 1;
    config.dst_mode = 0;
    config.ms_id_mode = 1;
    uint32_t channel_id = channel_impl->get_channel_id();
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::trans_mem(
        instr++, rep_buf_read->get_dst().id(), const_value2_var_map.at(dep.ccu_res_space_token_info).id(),
        rep_buf_read->get_src().addr.id(), rep_buf_read->get_src().token.id(), rep_buf_read->get_len().id(),
        const_value2_var_map.at(channel_id).id(), notify, reduce, config, rep_buf_read->get_sem().id(),
        rep_buf_read->get_mask());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_write_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_write* rep_write)
{
    CHK_PTR_NULL(rep_write);
    CHK_PTR_NULL(ccu_kernel);
    ccu_channel channel_impl(rep_write->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_write->get_channel()), HCCL_E_INTERNAL);
    ccu_v2::trans_mem_notify_info notify = {};
    ccu_v2::trans_mem_reduce_info reduce = {};
    ccu_v2::trans_mem_config config = {};
    config.dma_op_code = URMA_DMA_OP_WRITE; // UB URMA WQEBB opcode: write
    config.src_mode = 0;
    config.dst_mode = 1;

    if (rep_write->get_reduce_flag() == 1) {
        config.udf_enable = 1;
        reduce.udf_type = 0;
        reduce.reduce_data_type = rep_write->get_data_type();
        reduce.reduce_op_code = rep_write->get_op_type();
    }
    uint32_t channel_id = channel_impl->get_channel_id();
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();

    uint64_t rmt_ccu_res_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_ccu_res_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    notify.xnt_id = const_value2_var_map.at(rmt_ccu_res_token).id();
    ccu_v2::trans_mem(
        instr++, rep_write->get_rem().addr.id(), rep_write->get_rem().token.id(), rep_write->get_loc().addr.id(),
        rep_write->get_loc().token.id(), rep_write->get_len().id(), const_value2_var_map.at(channel_id).id(), notify,
        reduce, config, rep_write->get_sem().id(), rep_write->get_mask());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_read_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_read* rep_rem_mem)
{
    CHK_PTR_NULL(rep_rem_mem);
    CHK_PTR_NULL(ccu_kernel);
    ccu_channel channel_impl(rep_rem_mem->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_rem_mem->get_channel()), HCCL_E_INTERNAL);

    ccu_v2::trans_mem_notify_info notify = {};
    ccu_v2::trans_mem_reduce_info reduce = {};
    ccu_v2::trans_mem_config config = {};
    config.dma_op_code = URMA_DMA_OP_READ; // UB URMA WQEBB opcode: read

    if (rep_rem_mem->get_reduce_flag() == 1) {
        config.udf_enable = 1;
        reduce.udf_type = 0;
        reduce.reduce_data_type = rep_rem_mem->get_data_type();
        reduce.reduce_op_code = rep_rem_mem->get_op_type();
    }
    uint32_t channel_id = channel_impl->get_channel_id();
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::trans_mem(
        instr++, rep_rem_mem->get_loc().addr.id(), rep_rem_mem->get_loc().token.id(), rep_rem_mem->get_rem().addr.id(),
        rep_rem_mem->get_rem().token.id(), rep_rem_mem->get_len().id(), const_value2_var_map.at(channel_id).id(),
        notify, reduce, config, rep_rem_mem->get_sem().id(), rep_rem_mem->get_mask());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_rem_mem_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_mem* rep_rem_mem)
{
    CHK_PTR_NULL(rep_rem_mem);
    ccu_channel channel_impl(rep_rem_mem->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_rem_mem->get_channel()), HCCL_E_INTERNAL);

    uint64_t addr{0};
    uint32_t size{0}, token_id{0}, token_value{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(addr, size, token_id, token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_rep_rem_mem][%s] failed to get remote buffer, channelHandle[0x%llx].", __func__,
            rep_rem_mem->get_channel()),
        HCCL_E_INTERNAL); // 当前认为channel只持有一个buffer

    auto tokenInfo = get_token(token_id, token_value, 1);

    ccu_v2::load_imd_to_xn(instr++, rep_rem_mem->get_rem().addr.id(), addr);
    ccu_v2::load_imd_to_xn(instr++, rep_rem_mem->get_rem().token.id(), tokenInfo);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loc_cpy_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_cpy* ccu_rep_loc_cpy, const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_loc_cpy);
    if (ccu_rep_loc_cpy->get_reduce_flag() == 0 && ccu_rep_loc_cpy->get_use_ccu_buffer() == true) {
        uint16_t first_buf_id = ccu_rep_loc_cpy->get_first_buf_id();
        ccu_v2::cache_config cache_config{0x0, 0x0};
        trans_loc_mem_to_loc_mem(
            instr++, ccu_rep_loc_cpy->get_dst_addr_id(), ccu_rep_loc_cpy->get_dst_token_id(),
            ccu_rep_loc_cpy->get_src_addr_id(), ccu_rep_loc_cpy->get_src_token_id(), ccu_rep_loc_cpy->get_len_id(),
            first_buf_id, ccu_rep_loc_cpy->get_used_buf_num(), ccu_rep_loc_cpy->get_sem_id(),
            ccu_rep_loc_cpy->get_mask(), cache_config, cache_config);
    } else {
        // 使用旧接口或带规约场景，都走环回
        ccu_v2::trans_mem_notify_info notify = {};
        ccu_v2::trans_mem_reduce_info reduce = {};
        ccu_v2::trans_mem_config config = {};
        config.dma_op_code = URMA_DMA_OP_WRITE; // UB URMA WQEBB opcode: write
        if (ccu_rep_loc_cpy->get_reduce_flag() == 1) {
            config.udf_enable = 1;
            reduce.udf_type = 0;
            reduce.reduce_data_type = ccu_rep_loc_cpy->get_data_type();
            reduce.reduce_op_code = ccu_rep_loc_cpy->get_op_type();
        }
        const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
        ccu_v2::trans_mem(
            instr++, ccu_rep_loc_cpy->get_dst_addr_id(), ccu_rep_loc_cpy->get_dst_token_id(),
            ccu_rep_loc_cpy->get_src_addr_id(), ccu_rep_loc_cpy->get_src_token_id(), ccu_rep_loc_cpy->get_len_id(),
            const_value2_var_map.at(dep.reserve_channal_id[0]).id(), notify, reduce, config,
            ccu_rep_loc_cpy->get_sem_id(), ccu_rep_loc_cpy->get_mask());
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_buf_write_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_write* ccuRepBufWrite, const trans_dep& dep)
{
    CHK_PTR_NULL(ccuRepBufWrite);
    CHK_PTR_NULL(ccu_kernel);
    ccu_v2::trans_mem_notify_info notify = {};
    ccu_v2::trans_mem_reduce_info reduce = {};
    ccu_v2::trans_mem_config config = {};
    config.dma_op_code = URMA_DMA_OP_WRITE; // UB URMA WQEBB opcode: write
    config.src_mode = 0;                    // 不偏移
    config.dst_mode = 1;                    // 偏移 256K
    config.ms_id_mode = 1;                  // 使用msId模式
    ccu_channel channel_impl(ccuRepBufWrite->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", ccuRepBufWrite->get_channel()), HCCL_E_INTERNAL);

    uint32_t channel_id = channel_impl->get_channel_id();
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();

    uint64_t rmt_ccu_res_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_ccu_res_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    notify.xnt_id = const_value2_var_map.at(rmt_ccu_res_token).id();
    HCCL_INFO(
        "DstTokenXnId[%u], NotifyTokenXnId[%u]", const_value2_var_map.at(dep.ccu_res_space_token_info).id(),
        notify.xnt_id);
    ccu_v2::trans_mem(
        instr++, ccuRepBufWrite->get_dst().addr.id(), ccuRepBufWrite->get_dst().token.id(),
        ccuRepBufWrite->get_src().id(), const_value2_var_map.at(dep.ccu_res_space_token_info).id(),
        ccuRepBufWrite->get_len().id(), const_value2_var_map.at(channel_id).id(), notify, reduce, config,
        ccuRepBufWrite->get_sem().id(), ccuRepBufWrite->get_mask());

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_buf_reduce_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_reduce* ccu_rep_buf_reduce)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(ccu_rep_buf_reduce);
    if (ccu_rep_buf_reduce->get_count() > ccu_reduce_max_ms ||
        ccu_rep_buf_reduce->get_mem().size() > ccu_reduce_max_ms) {
        HCCL_ERROR(
            "count[%u] and mem size[%zu] must be less than %u", ccu_rep_buf_reduce->get_count(),
            ccu_rep_buf_reduce->get_mem().size(), ccu_reduce_max_ms);
        return HCCL_E_PARA;
    }
    if (ccu_rep_buf_reduce->get_count() < ccu_reduce_min_ms) {
        HCCL_ERROR("count[%u] must be at least %u", ccu_rep_buf_reduce->get_count(), ccu_reduce_min_ms);
        return HCCL_E_PARA;
    }

    uint16_t ms_id[ccu_reduce_max_ms] = {0};
    const auto& mem = ccu_rep_buf_reduce->get_mem();
    for (uint16_t i = 0; i < mem.size(); i++) {
        ms_id[i] = mem[i].id();
    }

    if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_sum) {
        if (ccu_rep_buf_reduce->get_output_data_type() == 1) { // 1是fp16
            ccu_v2::reduce_add(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_output_data_type(),
                ccu_rep_buf_reduce->get_data_type(), ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
                ccu_rep_buf_reduce->get_xn_id_length().id());
        } else if (ccu_rep_buf_reduce->get_output_data_type() == 2) { // 2是bf16
            ccu_v2::reduce_add(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_output_data_type(),
                ccu_rep_buf_reduce->get_data_type(), ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
                ccu_rep_buf_reduce->get_xn_id_length().id());
        } else {
            ccu_v2::reduce_add(
                instr++, ms_id, ccu_rep_buf_reduce->get_count(), 0, ccu_rep_buf_reduce->get_data_type(),
                ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
                ccu_rep_buf_reduce->get_xn_id_length().id());
        }
    } else if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_max) {
        ccu_v2::reduce_max(
            instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_data_type(),
            ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
            ccu_rep_buf_reduce->get_xn_id_length().id());
    } else if (ccu_rep_buf_reduce->get_op_type() == ccu_reduce_min) {
        ccu_v2::reduce_min(
            instr++, ms_id, ccu_rep_buf_reduce->get_count(), ccu_rep_buf_reduce->get_data_type(),
            ccu_rep_buf_reduce->get_sem().id(), ccu_rep_buf_reduce->get_mask(),
            ccu_rep_buf_reduce->get_xn_id_length().id());
    } else {
        HCCL_ERROR("[ccu_rep_buf_reduce_translate] Unsupported opType[%d]", ccu_rep_buf_reduce->get_op_type());
        return HCCL_E_PARA;
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loc_record_event_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_record_event* ccu_rep_loc_record_event)
{
    CHK_PTR_NULL(ccu_rep_loc_record_event);
    ccu_v2::set_cke(instr++, ccu_rep_loc_record_event->get_event().id(), ccu_rep_loc_record_event->get_mask(), 0, 0, 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loc_wait_event_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_event* ccu_rep_loc_wait_event)
{
    CHK_PTR_NULL(ccu_rep_loc_wait_event);
    // 需要profiling的使用SetCKEInstr, 否则使用ClearCKEInstr
    if (ccu_rep_loc_wait_event->get_is_profiling()) {
        ccu_v2::set_cke(instr++, 0, 0, ccu_rep_loc_wait_event->get_event().id(), ccu_rep_loc_wait_event->get_mask(), 1);
    } else {
        ccu_v2::clear_cke(
            instr++, 0, 0, ccu_rep_loc_wait_event->get_event().id(), ccu_rep_loc_wait_event->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loc_wait_notify_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_notify* ccu_rep_loc_wait_notify)
{
    CHK_PTR_NULL(ccu_rep_loc_wait_notify);
    if (ccu_rep_loc_wait_notify->get_is_profiling()) {
        ccu_v2::set_cke(
            instr++, 0, 0, ccu_rep_loc_wait_notify->get_notify().id(), ccu_rep_loc_wait_notify->get_mask(), 1);
    } else {
        ccu_v2::clear_cke(
            instr++, 0, 0, ccu_rep_loc_wait_notify->get_notify().id(), ccu_rep_loc_wait_notify->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_rem_wait_sem_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_wait_sem* ccu_rep_rem_wait_sem)
{
    CHK_PTR_NULL(ccu_rep_rem_wait_sem);
    ccu_channel channel_impl(ccu_rep_rem_wait_sem->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", ccu_rep_rem_wait_sem->get_channel()), HCCL_E_INTERNAL);

    uint32_t locCkeId{0};
    CHK_PRT_RET(
        channel_impl->get_loc_cke_by_index(ccu_rep_rem_wait_sem->get_sem_index(), locCkeId) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_rep_rem_wait_sem][%s] failed to get loc cke id, channelHandle[0x%llx], semIndex[%u].", __func__,
            ccu_rep_rem_wait_sem->get_channel(), ccu_rep_rem_wait_sem->get_sem_index()),
        HCCL_E_INTERNAL);

    // 需要profiling的使用SetCKEInstr, 否则使用ClearCKEInstr
    if (ccu_rep_rem_wait_sem->get_is_profiling()) {
        ccu_v2::set_cke(instr++, 0, 0, locCkeId, ccu_rep_rem_wait_sem->get_mask(), 1);
    } else {
        ccu_v2::clear_cke(instr++, 0, 0, locCkeId, ccu_rep_rem_wait_sem->get_mask(), 1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_rem_post_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_var* ccu_rep_rem_post_var)
{
    CHK_PTR_NULL(ccu_rep_rem_post_var);
    CHK_PTR_NULL(ccu_kernel);
    ccu_channel channel_impl(ccu_rep_rem_post_var->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", ccu_rep_rem_post_var->get_channel()), HCCL_E_INTERNAL);

    uint32_t channel_id = channel_impl->get_channel_id();
    uint64_t rmt_signal_addr{0};
    uint64_t rmt_var_addr{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_addr_by_index(ccu_rep_rem_post_var->get_sem_index(), rmt_signal_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote signal addr, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_var->get_channel()),
        HCCL_E_INTERNAL);
    CHK_PRT_RET(
        channel_impl->get_rmt_var_addr_by_index(ccu_rep_rem_post_var->get_param_index(), rmt_var_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote var addr, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_var->get_channel()),
        HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);

    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::trans_mem_notify_info notify = {};
    notify.xn_id = const_value2_var_map.at(rmt_signal_addr).id();
    notify.xnt_id = const_value2_var_map.at(rmt_token).id();
    notify.value = ccu_rep_rem_post_var->get_mask();

    ccu_v2::sync_wt_x(
        instr++, const_value2_var_map.at(rmt_var_addr).id(), const_value2_var_map.at(rmt_token).id(),
        ccu_rep_rem_post_var->get_param().id(), const_value2_var_map.at(channel_id).id(), notify, 0, 0);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_rem_post_sem_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_sem* ccu_rep_rem_post_sem,
    [[maybe_unused]] const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_rem_post_sem);
    CHK_PTR_NULL(ccu_kernel);
    ccu_channel channel_impl(ccu_rep_rem_post_sem->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", ccu_rep_rem_post_sem->get_channel()), HCCL_E_INTERNAL);

    uint64_t rmt_signal_addr{0};
    uint32_t channel_id = channel_impl->get_channel_id();
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_addr_by_index(ccu_rep_rem_post_sem->get_sem_index(), rmt_signal_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote signal addr, channelHandle[0x%llx].", __func__,
            ccu_rep_rem_post_sem->get_channel()),
        HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);

    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::trans_mem_notify_info notify = {};
    notify.xn_id = const_value2_var_map.at(rmt_signal_addr).id();
    notify.xnt_id = const_value2_var_map.at(rmt_token).id();
    notify.value = ccu_rep_rem_post_sem->get_mask();

    ccu_v2::sync_wt_x(instr++, notify, const_value2_var_map.at(channel_id).id(), 0, 0);

    return HcclResult::HCCL_SUCCESS;
}

constexpr uint16_t CCU_RESOURCE_CKE_MASK_LENGTH = 2;
HcclResult ccu_ins_generater_v2::ccu_rep_record_shared_notify_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_record_shared_notify* ccu_rep_record_shared_notify,
    const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_record_shared_notify);
    CHK_PTR_NULL(ccu_kernel);
    if (ccu_rep_record_shared_notify->get_notify().die_id() != dep.die_id) {
        // 计算对端cke地址，对端Xn基地址 + 256K + (ccu_rep_record_shared_notify->get_notify().id() * 8)
        uint64_t ckeAddr = dep.xn_base_addr[ccu_rep_record_shared_notify->get_notify().die_id()] +
                           CCU_V2_XN_RESERVE_SIZE +
                           (ccu_rep_record_shared_notify->get_notify().id() * CCU_CKE_PER_SIZE);
        const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
        ccu_v2::cache_config cache_config{0x0, 0x0};
        ccu_v2::store_x_to_mem(
            instr++,
            const_value2_var_map.at(ckeAddr).id(),                                  // 对端cke xn
            const_value2_var_map.at(dep.mem_token_info).id(),                       // 对端cke xn token
            const_value2_var_map.at(ccu_rep_record_shared_notify->get_mask()).id(), // cke mask
            const_value2_var_map.at(CCU_RESOURCE_CKE_MASK_LENGTH).id(),             // cke mask，2字节
            cache_config,
            dep.comm_signal, // store完成后同步的cke
            1);              // store完成后同步的cke mask
        ccu_v2::set_cke(instr++, 0, 0, dep.comm_signal, 1, 1);
    } else {
        ccu_v2::set_cke(
            instr++, ccu_rep_record_shared_notify->get_notify().id(), ccu_rep_record_shared_notify->get_mask(), 0, 0,
            1);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_add_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_add* ccu_rep_add, const trans_dep& dep)
{
    HCCL_INFO("Use table-driven translate for add");
    UNUSED(dep);
    CHK_PTR_NULL(ccu_rep_add);

    // {isImmed, isSelf, dstVar, srcAVar, srcBAddr} — 按 add_sub_type 枚举顺序
    static constexpr struct {
        bool isImmed;
        bool isSelf;
        bool dstVar;
        bool srcAVar;
        bool srcBAddr;
    } table[] = {
        {false, false, false, false, false}, // INVALID
        {false, false, false, false, false}, // ADDR_PLUS_VAR_TO_ADDR
        {false, false, false, false, true},  // ADDR_PLUS_ADDR_TO_ADDR
        {false, false, true, true, false},   // VAR_PLUS_VAR_TO_VAR
        {false, true, false, false, false},  // SELF_ADD_ADDRESS
        {false, true, true, true, false},    // SELF_ADD_VARIABLE
        {true, false, true, true, false},    // VAR_PLUS_IMMED_TO_VAR
        {true, false, false, false, false},  // ADDR_PLUS_IMMED_TO_ADDR
        {false, false, false, true, false},  // VAR_PLUS_VAR_TO_ADDR
        {true, true, false, false, false},   // SELF_ADD_IMMED_ADDRESS
        {true, true, true, true, false},     // SELF_ADD_IMMED_VARIABLE
        {true, false, false, true, false},   // VAR_PLUS_IMMED_TO_ADDR
        {true, false, true, false, false},   // ADDR_PLUS_IMMED_TO_VAR
        {false, false, true, false, true},   // ADDR_PLUS_ADDR_TO_VAR
    };

    auto idx = static_cast<size_t>(ccu_rep_add->get_sub_type());
    if (idx == 0 || idx >= sizeof(table) / sizeof(table[0])) {
        HCCL_ERROR("Invalid add_sub_type[%d]", idx);
        return HCCL_E_PARA;
    }
    const auto& info = table[idx];

    uint16_t src_a_id = info.srcAVar ? ccu_rep_add->get_var_a().id() : ccu_rep_add->get_addr_a().id();
    uint16_t dst_id =
        info.isSelf ? src_a_id : (info.dstVar ? ccu_rep_add->get_var_c().id() : ccu_rep_add->get_addr_c().id());

    if (info.isImmed) {
        ccu_v2::add_i(instr++, dst_id, src_a_id, ccu_rep_add->get_immed_b(), 0, 0);
    } else {
        uint16_t src_b_id = info.srcBAddr ? ccu_rep_add->get_addr_b().id() : ccu_rep_add->get_var_b().id();
        ccu_v2::add(instr++, dst_id, src_a_id, src_b_id, 0, 0);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_assign_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_assign* ccu_rep_assign, const trans_dep& dep)
{
    UNUSED(dep);
    CHK_PTR_NULL(ccu_rep_assign);
    switch (ccu_rep_assign->get_sub_type()) {
        case assign_sub_type::imd_to_variable: {
            ccu_v2::assign_i(instr++, ccu_rep_assign->get_var_a().id(), ccu_rep_assign->get_immed());
            break;
        }
        case assign_sub_type::imd_to_addr: {
            ccu_v2::assign_i(instr++, ccu_rep_assign->get_addr_a().id(), ccu_rep_assign->get_immed());
            break;
        }
        case assign_sub_type::var_to_addr: {
            ccu_v2::assign(instr++, ccu_rep_assign->get_addr_a().id(), ccu_rep_assign->get_var_a().id());
            break;
        }
        case assign_sub_type::addr_to_addr: {
            ccu_v2::assign(instr++, ccu_rep_assign->get_addr_b().id(), ccu_rep_assign->get_addr_a().id());
            break;
        }
        case assign_sub_type::var_to_var: {
            ccu_v2::assign(instr++, ccu_rep_assign->get_var_b().id(), ccu_rep_assign->get_var_a().id());
            break;
        }
        default: {
            HCCL_ERROR("Invalid Assign, subType[%d]", static_cast<int>(ccu_rep_assign->get_sub_type()));
            return HCCL_E_PARA;
        }
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_mul_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_mul* ccu_rep_mul)
{
    CHK_PTR_NULL(ccu_rep_mul);

    // {isImmed, isSelf, dstVar, srcAVar, srcBAddr} — 按 MulSubType 枚举顺序
    static constexpr struct {
        bool isImmed;
        bool isSelf;
        bool dstVar;
        bool srcAVar;
        bool srcBAddr;
    } table[] = {
        {false, false, false, false, false}, // INVALID
        {false, false, true, true, false},   // VAR_MUL_VAR_TO_VAR
        {true, false, true, true, false},    // VAR_MUL_IMMED_TO_VAR
        {false, true, true, true, false},    // SELF_MUL_VAR_VARIABLE
        {true, true, true, true, false},     // SELF_MUL_IMMED_VARIABLE
        {false, false, false, true, false},  // VAR_MUL_VAR_TO_ADDR
        {false, false, false, true, true},   // VAR_MUL_ADDR_TO_ADDR
        {true, false, false, true, false},   // VAR_MUL_IMMED_TO_ADDR
        {true, false, false, false, false},  // ADDR_MUL_IMMED_TO_ADDR
        {false, true, false, false, false},  // SELF_MUL_VAR_ADDRESS
        {true, true, false, false, false},   // SELF_MUL_IMMED_ADDRESS
        {true, false, true, false, false},   // ADDR_MUL_IMMED_TO_VAR
    };

    auto idx = static_cast<size_t>(ccu_rep_mul->get_sub_type());
    if (idx == 0 || idx >= sizeof(table) / sizeof(table[0])) {
        HCCL_ERROR("Invalid Mul, subType[%d]", static_cast<int>(ccu_rep_mul->get_sub_type()));
        return HCCL_E_PARA;
    }
    const auto& info = table[idx];

    uint16_t src_a_id = info.srcAVar ? ccu_rep_mul->get_var_a().id() : ccu_rep_mul->get_addr_a().id();
    uint16_t dst_id =
        info.isSelf ? src_a_id : (info.dstVar ? ccu_rep_mul->get_var_c().id() : ccu_rep_mul->get_addr_c().id());

    if (info.isImmed) {
        ccu_v2::mul_i(instr++, dst_id, src_a_id, ccu_rep_mul->get_immed_b(), 0, 0);
    } else {
        uint16_t src_b_id = info.srcBAddr ? ccu_rep_mul->get_addr_b().id() : ccu_rep_mul->get_var_b().id();
        ccu_v2::mul(instr++, dst_id, src_a_id, src_b_id, 0, 0);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_sub_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sub* ccu_rep_sub)
{
    CHK_PTR_NULL(ccu_rep_sub);

    // {isImmed, isSelf, dstVar, srcAVar, srcBAddr} — 按 MinusSubType 枚举顺序
    static constexpr struct {
        bool isImmed;
        bool isSelf;
        bool dstVar;
        bool srcAVar;
        bool srcBAddr;
    } table[] = {
        {false, false, false, false, false}, // INVALID
        {false, false, true, true, false},   // VAR_MINUS_VAR_TO_VAR
        {true, false, true, true, false},    // VAR_MINUS_IMMED_TO_VAR
        {false, true, true, true, false},    // SELF_SUB_VAR_VARIABLE
        {true, true, true, true, false},     // SELF_SUB_IMMED_VARIABLE
        {false, false, false, false, false}, // ADDR_MINUS_VAR_TO_ADDR
        {true, false, false, false, false},  // ADDR_MINUS_IMMED_TO_ADDR
        {false, true, false, false, false},  // SELF_SUB_VAR_ADDRESS
        {true, true, false, false, false},   // SELF_SUB_IMMED_ADDRESS
        {true, false, false, true, false},   // VAR_MINUS_IMMED_TO_ADDR
        {true, false, true, false, false},   // ADDR_MINUS_IMMED_TO_VAR
    };

    auto idx = static_cast<size_t>(ccu_rep_sub->get_sub_type());
    if (idx == 0 || idx >= sizeof(table) / sizeof(table[0])) {
        HCCL_ERROR("Invalid Sub, subType[%d]", static_cast<int>(ccu_rep_sub->get_sub_type()));
        return HCCL_E_PARA;
    }
    const auto& info = table[idx];

    uint16_t src_a_id = info.srcAVar ? ccu_rep_sub->get_var_a().id() : ccu_rep_sub->get_addr_a().id();
    uint16_t dst_id =
        info.isSelf ? src_a_id : (info.dstVar ? ccu_rep_sub->get_var_c().id() : ccu_rep_sub->get_addr_c().id());

    if (info.isImmed) {
        ccu_v2::sub_i(instr++, dst_id, src_a_id, ccu_rep_sub->get_immed_b(), 0, 0);
    } else {
        uint16_t src_b_id = info.srcBAddr ? ccu_rep_sub->get_addr_b().id() : ccu_rep_sub->get_var_b().id();
        ccu_v2::sub(instr++, dst_id, src_a_id, src_b_id, 0, 0);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_and_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* ccu_rep_and,
    [[maybe_unused]] const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_and);
    switch (ccu_rep_and->get_sub_type()) {
        case and_sub_type::var_and_var_to_var: {
            ccu_v2::and_op(
                instr++, ccu_rep_and->get_var_c().id(), ccu_rep_and->get_var_a().id(), ccu_rep_and->get_var_b().id(), 0,
                0);
            break;
        }
        case and_sub_type::self_and_var_variable: {
            ccu_v2::and_op(
                instr++, ccu_rep_and->get_var_c().id(), ccu_rep_and->get_var_c().id(), ccu_rep_and->get_var_b().id(), 0,
                0);
            break;
        }
        default: {
            HCCL_ERROR("Invalid And, subType[%d]", static_cast<int>(ccu_rep_and->get_sub_type()));
            return HCCL_E_PARA;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_not_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* ccu_rep_not,
    [[maybe_unused]] const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_not);
    switch (ccu_rep_not->get_sub_type()) {
        case not_sub_type::var_equals_not_var: {
            ccu_v2::not_op(instr++, ccu_rep_not->get_var_c().id(), ccu_rep_not->get_var_b().id(), 0, 0);
            break;
        }
        default: {
            HCCL_ERROR("Invalid Not, subType[%d]", static_cast<int>(ccu_rep_not->get_sub_type()));
            return HCCL_E_PARA;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_or_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* ccu_rep_or,
    [[maybe_unused]] const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_or);
    switch (ccu_rep_or->get_sub_type()) {
        case or_sub_type::var_or_var_to_var: {
            ccu_v2::or_op(
                instr++, ccu_rep_or->get_var_c().id(), ccu_rep_or->get_var_a().id(), ccu_rep_or->get_var_b().id(), 0,
                0);
            break;
        }
        case or_sub_type::self_or_var_variable: {
            ccu_v2::or_op(
                instr++, ccu_rep_or->get_var_c().id(), ccu_rep_or->get_var_c().id(), ccu_rep_or->get_var_b().id(), 0,
                0);
            break;
        }
        default: {
            HCCL_ERROR("Invalid Or, subType[%d]", static_cast<int>(ccu_rep_or->get_sub_type()));
            return HCCL_E_PARA;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_xor_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* ccu_rep_xor,
    [[maybe_unused]] const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_xor);
    switch (ccu_rep_xor->get_sub_type()) {
        case xor_sub_type::var_xor_var_to_var: {
            ccu_v2::xor_op(
                instr++, ccu_rep_xor->get_var_c().id(), ccu_rep_xor->get_var_a().id(), ccu_rep_xor->get_var_b().id(), 0,
                0);
            break;
        }
        case xor_sub_type::self_xor_var_variable: {
            ccu_v2::xor_op(
                instr++, ccu_rep_xor->get_var_c().id(), ccu_rep_xor->get_var_c().id(), ccu_rep_xor->get_var_b().id(), 0,
                0);
            break;
        }
        default: {
            HCCL_ERROR("Invalid Xor, subType[%d]", static_cast<int>(ccu_rep_xor->get_sub_type()));
            return HCCL_E_PARA;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_sh_l_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* ccu_rep_sh_l, const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_sh_l);
    if (ccu_rep_sh_l->get_shift_type() == shift_type::logical_shift) {
        switch (ccu_rep_sh_l->get_shift_sub_type()) {
            case shift_sub_type::var_equals_var_shift_var: {
                ccu_v2::sll(
                    instr++, ccu_rep_sh_l->get_var_d().id(), ccu_rep_sh_l->get_var_n().id(),
                    ccu_rep_sh_l->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::var_shift_assign_var: {
                ccu_v2::sll(
                    instr++, ccu_rep_sh_l->get_var_d().id(), ccu_rep_sh_l->get_var_d().id(),
                    ccu_rep_sh_l->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::addr_equals_var_shift_var: {
                ccu_v2::sll(
                    instr++, ccu_rep_sh_l->get_address_d().id(), ccu_rep_sh_l->get_var_n().id(),
                    ccu_rep_sh_l->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::addr_shift_assign_var: {
                ccu_v2::sll(
                    instr++, ccu_rep_sh_l->get_address_d().id(), ccu_rep_sh_l->get_address_d().id(),
                    ccu_rep_sh_l->get_var_m().id(), 0, 0);
                break;
            }
            default: {
                HCCL_ERROR(
                    "Invalid Shift left, shiftSubType[%d]", static_cast<int>(ccu_rep_sh_l->get_shift_sub_type()));
                return HCCL_E_PARA;
            }
        }
    } else {
        HCCL_ERROR("Invalid Shift left, shiftType[%d]", static_cast<int>(ccu_rep_sh_l->get_shift_type()));
        return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_sh_r_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* ccu_rep_sh_r, const trans_dep& dep)
{
    CHK_PTR_NULL(ccu_rep_sh_r);
    if (ccu_rep_sh_r->get_shift_type() == shift_type::logical_shift) {
        switch (ccu_rep_sh_r->get_shift_sub_type()) {
            case shift_sub_type::var_equals_var_shift_var: {
                ccu_v2::srl(
                    instr++, ccu_rep_sh_r->get_var_d().id(), ccu_rep_sh_r->get_var_n().id(),
                    ccu_rep_sh_r->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::var_shift_assign_var: {
                ccu_v2::srl(
                    instr++, ccu_rep_sh_r->get_var_d().id(), ccu_rep_sh_r->get_var_d().id(),
                    ccu_rep_sh_r->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::addr_equals_var_shift_var: {
                ccu_v2::srl(
                    instr++, ccu_rep_sh_r->get_address_d().id(), ccu_rep_sh_r->get_var_n().id(),
                    ccu_rep_sh_r->get_var_m().id(), 0, 0);
                break;
            }
            case shift_sub_type::addr_shift_assign_var: {
                ccu_v2::srl(
                    instr++, ccu_rep_sh_r->get_address_d().id(), ccu_rep_sh_r->get_address_d().id(),
                    ccu_rep_sh_r->get_var_m().id(), 0, 0);
                break;
            }
            default: {
                HCCL_ERROR(
                    "Invalid Shift right, shiftSubType[%d]", static_cast<int>(ccu_rep_sh_r->get_shift_sub_type()));
                return HCCL_E_PARA;
            }
        }
    } else {
        HCCL_ERROR("Invalid Shift right, shiftType[%d]", static_cast<int>(ccu_rep_sh_r->get_shift_type()));
        return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_func_block_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id,
    ccu_rep_func_block* func_blockPtr, const trans_dep& dep, uint32_t step)
{
    CHK_PTR_NULL(func_blockPtr);
    std::vector<ccu_rep_arg>& in_args = func_blockPtr->get_in_args();
    (void)in_args;
    std::vector<ccu_rep_arg>& out_args = func_blockPtr->get_out_args();
    ccu_rep_reference_manager* func_manager = func_blockPtr->get_func_manager();
    CHK_PTR_NULL(func_manager);
    uint16_t callLayer = func_blockPtr->get_call_layer();
    if (step == 0) {
        // 函数入口为nop
        ccu_v2::nop(instr++);
        cur_instr_id++;
    } else if (step == 1) {
        // 处理输出的参数
        uint32_t iOutArg = 0;
        const auto& funcOut = func_manager->get_func_out();
        if (iOutArg >= funcOut.size()) {
            HCCL_ERROR("[FuncBlock] out arg index %u >= funcOut size %zu", iOutArg, funcOut.size());
            return HCCL_E_PARA;
        }
        for (uint32_t i = 0; i < out_args.size(); i++) {
            if (out_args[i].type == ccu_arg_type::variable) {
                ccu_v2::add(instr++, funcOut[iOutArg++].id(), out_args[i].var.id(), dep.reserve_xn_id);
                cur_instr_id++;
            } else if (out_args[i].type == ccu_arg_type::variable_list) {
                for (uint32_t j = 0; j < out_args[i].var_list.size(); j++) {
                    ccu_v2::add(instr++, funcOut[iOutArg++].id(), out_args[i].var_list[j].id(), dep.reserve_xn_id);
                    cur_instr_id++;
                }
            }
        }

        // 返回调用处
        uint32_t relJmpInstrNum = REL_JMP_INSTR_NUM; // relJmp需要9条指令
        ccu_v2::rel_jmp(
            instr, func_manager->get_func_ret(callLayer).id(), cur_instr_id + relJmpInstrNum, dep.comm_xn[0],
            dep.comm_xn[1]);
        instr += relJmpInstrNum;
        cur_instr_id += relJmpInstrNum;
        ccu_v2::jump(instr++, func_manager->get_func_ret(callLayer).id(), dep.reserve_xn_id, dep.reserve_xn_id, 0);
        cur_instr_id++;
    } else {
        HCCL_ERROR("Unsupported step[%d] for ccu_rep_func_block_translate", step);
        return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

void ccu_ins_generater_v2::load_func_call_in_args(
    ccu_instr* instr, std::vector<ccu_rep_arg>& in_args, std::vector<variable>& formal_ins,
    uint16_t reserve_xn_id) const
{
    uint32_t idx = 0;
    for (uint32_t i = 0; i < in_args.size(); i++) {
        if (in_args[i].type == ccu_arg_type::variable) {
            ccu_v2::add(instr + idx, formal_ins[idx].id(), in_args[i].var.id(), reserve_xn_id);
            idx++;
        } else if (in_args[i].type == ccu_arg_type::variable_list) {
            for (uint32_t j = 0; j < in_args[i].var_list.size(); j++) {
                ccu_v2::add(instr + idx, formal_ins[idx].id(), in_args[i].var_list[j].id(), reserve_xn_id);
                idx++;
            }
        }
    }
}

void ccu_ins_generater_v2::load_func_call_out_args(
    ccu_instr* instr, uint32_t offset, std::vector<ccu_rep_arg>& out_args, ccu_rep_reference_manager* func_manager,
    uint16_t reserve_xn_id) const
{
    uint32_t idx = 0;
    for (uint32_t i = 0; i < out_args.size(); i++) {
        if (out_args[i].type == ccu_arg_type::variable) {
            ccu_v2::add(
                instr + offset + idx, out_args[i].var.id(), func_manager->get_func_out()[idx].id(), reserve_xn_id);
            idx++;
        } else if (out_args[i].type == ccu_arg_type::variable_list) {
            for (uint32_t j = 0; j < out_args[i].var_list.size(); j++) {
                ccu_v2::add(
                    instr + offset + idx, out_args[i].var_list[j].id(), func_manager->get_func_out()[idx].id(),
                    reserve_xn_id);
                idx++;
            }
        }
    }
}

HcclResult ccu_ins_generater_v2::ccu_rep_func_call_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id,
    ccu_rep_func_call* func_call_ptr, const trans_dep& dep)
{
    (void)instr;
    (void)cur_instr_id;

    func_call_context ctx;
    CHK_RET(prepare_func_call_context(func_call_ptr, ctx));

    std::vector<ccu_rep_arg>& in_args = func_call_ptr->get_in_args();
    std::vector<ccu_rep_arg>& out_args = func_call_ptr->get_out_args();
    uint32_t in_arg_count = ctx.in_arg_count;
    ccu_rep_reference_manager* func_manager = ctx.func_manager;
    std::shared_ptr<ccu_rep_func_block>& func_block = ctx.func_block;
    ccu_instr* funcInstr = ctx.instr;
    std::vector<variable>& formal_ins = ctx.formal_ins;
    variable funcAddrVar = func_call_ptr->get_func_addr_var();
    int32_t callLayer = func_call_ptr->get_call_layer();
    uint16_t instr_id = func_call_ptr->start_instr_id();
    load_func_call_in_args(funcInstr, in_args, formal_ins, dep.reserve_xn_id);

    uint32_t locId = 0;
    if (func_block != nullptr) {
        ccu_v2::load_imd_to_xn(
            funcInstr + in_arg_count + locId++, func_manager->get_func_call().id(), func_block->start_instr_id());
    } else {
        ccu_v2::add(
            funcInstr + in_arg_count + locId++, func_manager->get_func_call().id(), funcAddrVar.id(),
            dep.reserve_xn_id);
    }
    ccu_v2::load_imd_to_xn(
        funcInstr + in_arg_count + locId++, func_manager->get_func_ret(callLayer).id(),
        instr_id + in_arg_count + FUNC_CALL_RET_OFFSET); // 需要指向函数返回位置
    ccu_v2::rel_jmp(
        funcInstr + in_arg_count + locId, func_manager->get_func_call().id(),
        instr_id + in_arg_count + FUNC_CALL_JMP_OFFSET, // Jmp的目标指令为其后11条指令
        dep.comm_xn[0], dep.comm_xn[1]);
    locId += REL_JMP_INSTR_NUM; // relJmp需要9条指令
    ccu_v2::jump(
        funcInstr + in_arg_count + locId++, func_manager->get_func_call().id(), dep.reserve_xn_id, dep.reserve_xn_id,
        0);
    ccu_v2::nop(funcInstr + in_arg_count + locId++);

    uint32_t extraInstrNum = get_instr_count(func_call_ptr->type());
    load_func_call_out_args(funcInstr, in_arg_count + extraInstrNum, out_args, func_manager, dep.reserve_xn_id);
    return HcclResult::HCCL_SUCCESS;
}

uint32_t get_relative_instr_id(uint32_t currentInstrId, uint32_t target_instr_id)
{
    static constexpr uint32_t CCU_INSTR_ID_SPACE_SIZE = 0x10000;
    if (target_instr_id > currentInstrId) {
        return target_instr_id - currentInstrId;
    } else {
        uint32_t diff = currentInstrId - target_instr_id;
        if (diff >= CCU_INSTR_ID_SPACE_SIZE) {
            HCCL_ERROR("Jump distance %u exceeds instr id space %u", diff, CCU_INSTR_ID_SPACE_SIZE);
            return 0;
        }
        return CCU_INSTR_ID_SPACE_SIZE - diff;
    }
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump* jump_ptr,
    const trans_dep& dep)
{
    (void)instr;
    (void)cur_instr_id;
    (void)dep;
    // 翻译直接跳转指令
    CHK_PTR_NULL(jump_ptr);
    std::shared_ptr<ccu_rep_jump_label> jump_label = jump_ptr->get_jump_label();
    CHK_PTR_NULL(jump_label);
    uint16_t instr_idOffset = get_relative_instr_id(jump_ptr->start_instr_id() + 1, jump_label->start_instr_id());
    ccu_v2::load_imd_to_xn(jump_ptr->get_instr() + 0, jump_ptr->get_target_instr_id().id(), instr_idOffset, 0, 0);
    //  无条件跳转
    ccu_v2::jump(
        jump_ptr->get_instr() + 1, jump_ptr->get_target_instr_id().id(), 0, 0,
        static_cast<uint16_t>(CCU_CONDITION_TYPE_DEFAULT) /* hcomm ConditionType::DEFAULT */);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_translate_v2_base(
    const ccu_instr* const& curInstr, const uint16_t& cur_instr_id, ccu_rep_jump_base* jump_base_ptr, uint64_t expected,
    const variable& condition, const variable& expected_var, condition_type cond_type) const
{
    (void)curInstr;
    (void)cur_instr_id;
    CHK_PTR_NULL(jump_base_ptr);
    ccu_instr* instr = jump_base_ptr->get_instr();
    uint16_t instr_id = jump_base_ptr->start_instr_id();
    variable& target_instr_id = jump_base_ptr->get_target_instr_id();
    std::shared_ptr<ccu_rep_jump_label> jump_label = jump_base_ptr->get_jump_label();
    CHK_PTR_NULL(jump_label);

    if (jump_base_ptr->is_compared_with_immd()) {
        ccu_v2::load_imd_to_xn(instr + 0, expected_var.id(), expected, 0, 0);
        uint16_t instr_idOffset = get_relative_instr_id(instr_id + 2, jump_label->start_instr_id()); // 2: jump指令偏移
        ccu_v2::load_imd_to_xn(instr + 1, target_instr_id.id(), instr_idOffset, 0, 0);
        ccu_v2::jump(
            instr + 2, target_instr_id.id(), condition.id(), expected_var.id(), // 2: jump指令偏移
            static_cast<uint16_t>(cond_type));
    } else {
        uint16_t instr_idOffset = get_relative_instr_id(instr_id + 1, jump_label->start_instr_id());
        ccu_v2::load_imd_to_xn(instr + 0, target_instr_id.id(), instr_idOffset, 0, 0);
        ccu_v2::jump(
            instr + 1, target_instr_id.id(), condition.id(), expected_var.id(), static_cast<uint16_t>(cond_type));
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_ne_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ne* jump_ne_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_ne_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_ne_ptr, jump_ne_ptr->get_expected_num(), jump_ne_ptr->get_condition(),
        jump_ne_ptr->get_expected_var(), condition_type::not_equal);
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_eq_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_eq* jump_eq_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_eq_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_eq_ptr, jump_eq_ptr->get_expected_num(), jump_eq_ptr->get_condition(),
        jump_eq_ptr->get_expected_var(), condition_type::equal);
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_le_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_le* jump_le_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_le_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_le_ptr, jump_le_ptr->get_expected_num(), jump_le_ptr->get_condition(),
        jump_le_ptr->get_expected_var(), condition_type::less_equal);
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_ge_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ge* jump_ge_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_ge_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_ge_ptr, jump_ge_ptr->get_expected_num(), jump_ge_ptr->get_condition(),
        jump_ge_ptr->get_expected_var(), condition_type::greater_equal);
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_gt_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_gt* jump_gt_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_gt_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_gt_ptr, jump_gt_ptr->get_expected_num(), jump_gt_ptr->get_condition(),
        jump_gt_ptr->get_expected_var(), condition_type::greater_than);
}

HcclResult ccu_ins_generater_v2::ccu_rep_jump_lt_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_lt* jump_lt_ptr,
    const trans_dep& dep)
{
    (void)dep;
    CHK_PTR_NULL(jump_lt_ptr);
    return ccu_rep_jump_translate_v2_base(
        instr, cur_instr_id, jump_lt_ptr, jump_lt_ptr->get_expected_num(), jump_lt_ptr->get_condition(),
        jump_lt_ptr->get_expected_var(), condition_type::less_than);
}

HcclResult ccu_ins_generater_v2::ccu_rep_loop_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop* loop_ptr)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(loop_ptr);
    auto loop_block = loop_ptr->get_loop_block();
    CHK_PTR_NULL(loop_block);
    ccu_v2::loop(
        instr++, loop_block->start_instr_id(), loop_block->start_instr_id() + loop_block->instr_count() - 1,
        loop_ptr->get_loop_iter_num().id(), loop_ptr->get_loop_gsa_offset().id(), loop_ptr->get_loop_param()->id());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::load_loop_call_arg(
    ccu_instr*& instr, const ccu_rep_arg& in_arg, const ccu_rep_arg& blk_arg) const
{
    switch (in_arg.type) {
        case ccu_arg_type::variable:
            ccu_v2::assign(instr++, blk_arg.var.id(), in_arg.var.id());
            break;
        case ccu_arg_type::variable_list:
            if (in_arg.var_list.size() != blk_arg.var_list.size()) {
                HCCL_ERROR(
                    "Mismatched Arg Size, in_arg.var_list.size[%zu], blk_arg.var_list.size[%zu]",
                    in_arg.var_list.size(), blk_arg.var_list.size());
                return HCCL_E_PARA;
            }
            for (uint32_t j = 0; j < in_arg.var_list.size(); j++) {
                ccu_v2::assign(instr++, blk_arg.var_list[j].id(), in_arg.var_list[j].id());
            }
            break;
        case ccu_arg_type::memory:
            load_addr_arg(instr, blk_arg.mem, in_arg.mem);
            break;
        case ccu_arg_type::local_addr:
            load_addr_arg(instr, blk_arg.local_addr_value, in_arg.local_addr_value);
            break;
        case ccu_arg_type::remote_addr:
            load_addr_arg(instr, blk_arg.remote_addr_value, in_arg.remote_addr_value);
            break;
        case ccu_arg_type::memory_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.mem_list, in_arg.mem_list));
            break;
        case ccu_arg_type::local_addr_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.local_addr_list, in_arg.local_addr_list));
            break;
        case ccu_arg_type::remote_addr_list:
            CHK_RET(load_addr_list_arg(instr, blk_arg.remote_addr_list, in_arg.remote_addr_list));
            break;
        default:
            HCCL_ERROR("Mismatched Arg Type, in_arg.type[%d]", static_cast<int>(in_arg.type));
            return HCCL_E_PARA;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loop_call_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id,
    ccu_rep_loop_call* loop_call_ptr, const trans_dep& dep)
{
    (void)cur_instr_id;
    (void)dep;
    CHK_PTR_NULL(loop_call_ptr);
    std::vector<ccu_rep_arg>& in_args = loop_call_ptr->get_in_args();
    auto loop_block = loop_call_ptr->get_loop_block();
    CHK_PTR_NULL(loop_block);

    for (uint32_t i = 0; i < in_args.size(); i++) {
        const ccu_rep_arg& in_arg = in_args[i];
        const ccu_rep_arg& blk_arg = loop_block->get_arg(i);
        if (in_arg.type != blk_arg.type) {
            HCCL_ERROR(
                "Mismatched Arg Type, in_arg.type[%d], blk_arg.type[%d]", static_cast<int>(in_arg.type),
                static_cast<int>(blk_arg.type));
            return HCCL_E_PARA;
        }
        CHK_RET(load_loop_call_arg(instr, in_arg, blk_arg));
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_set_loop_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_set_loop* set_loop_ptr)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(set_loop_ptr);
    // CCU V121 将executorId赋值给loop指令的Xp，上层计算的loopContextId实际上没有用
    ccu_v2::load_imd_to_xn(instr++, set_loop_ptr->loop_param.id(), set_loop_ptr->executor_value.id());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_load_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load* load_ptr, const trans_dep& dep)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(load_ptr);
    CHK_PTR_NULL(ccu_kernel);
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::cache_config cache_config{0x0, 0x0};

    // var寄存器的真实id在常量准备阶段（Register）无法获取，故不记录在常量表中，单独赋值处理
    ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], load_ptr->get_var().id()); // dst xn id
    ccu_v2::load_x_from_mem(
        instr++, dep.comm_xn[0], const_value2_var_map.at(load_ptr->get_addr()).id(),
        const_value2_var_map.at(dep.mem_token_info).id(),
        const_value2_var_map.at(HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_ptr->get_num()).id(), cache_config,
        dep.comm_signal, load_ptr->get_mask());
    ccu_v2::set_cke(instr++, 0, 0, dep.comm_signal, load_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_load_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_var* load_var_ptr,
    const trans_dep& dep)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(load_var_ptr);
    CHK_PTR_NULL(ccu_kernel);
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::cache_config cache_config{0x0, 0x0};

    // var寄存器的真实id在常量准备阶段（Register）无法获取，故不记录在常量表中，单独赋值处理
    ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], load_var_ptr->get_var().id()); // dst xn id
    ccu_v2::load_x_from_mem(
        instr++, dep.comm_xn[0], load_var_ptr->get_src().id(), const_value2_var_map.at(dep.mem_token_info).id(),
        const_value2_var_map.at(HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_var_ptr->get_num()).id(), cache_config,
        dep.comm_signal, load_var_ptr->get_mask());
    ccu_v2::set_cke(instr++, 0, 0, dep.comm_signal, load_var_ptr->get_mask(), 1);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_load_arg_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_arg* load_arg_ptr,
    const trans_dep& dep)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(load_arg_ptr);
    if (dep.is_func_block) {
        // Xn(var) = Xn(loadXnId) + 0
        ccu_v2::add(instr++, load_arg_ptr->get_var().id(), dep.load_xn_id, dep.reserve_xn_id);
    } else {
        ccu_v2::load_sqe_args_to_x(instr++, load_arg_ptr->get_var().id(), load_arg_ptr->get_arg_id());
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_nop_translate(
    [[maybe_unused]] ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_nop* nop_ptr,
    const trans_dep& dep)
{
    (void)cur_instr_id;
    (void)nop_ptr;
    (void)dep;
    ccu_v2::nop(instr++);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_store_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store* store_ptr, const trans_dep& dep)
{
    (void)cur_instr_id;
    CHK_PTR_NULL(store_ptr);
    CHK_PTR_NULL(ccu_kernel);
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    ccu_v2::cache_config cache_config{0x0, 0x0};

    // var寄存器的真实id在常量准备阶段（Register）无法获取，故不记录在常量表中，单独赋值处理
    ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], store_ptr->get_var().id()); // src xn id
    ccu_v2::store_x_to_mem(
        instr++, const_value2_var_map.at(store_ptr->get_addr()).id(), const_value2_var_map.at(dep.mem_token_info).id(),
        dep.comm_xn[0], const_value2_var_map.at(HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_ptr->get_num()).id(),
        cache_config, dep.comm_signal, store_ptr->get_mask());
    ccu_v2::set_cke(instr++, 0, 0, dep.comm_signal, store_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_store_var_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_var* store_var_ptr,
    const trans_dep& dep)
{
    CHK_PTR_NULL(store_var_ptr);
    CHK_PTR_NULL(ccu_kernel);
    (void)cur_instr_id;
    ccu_v2::cache_config cache_config{0x0, 0x0};

    // var寄存器的真实id在常量准备阶段（Register）无法获取，故不记录在常量表中，单独赋值处理
    ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], store_var_ptr->get_var().id());
    const auto& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    if (store_var_ptr->get_hscb_flag()) {
        ccu_v2::hscb_store_x_to_mem(
            instr++, store_var_ptr->get_dst().id(), dep.comm_xn[0],
            const_value2_var_map.at(HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_var_ptr->get_num()).id(), cache_config,
            dep.comm_signal, store_var_ptr->get_mask());
    } else {
        ccu_v2::store_x_to_mem(
            instr++, store_var_ptr->get_dst().id(), const_value2_var_map.at(dep.mem_token_info).id(), dep.comm_xn[0],
            const_value2_var_map.at(HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_var_ptr->get_num()).id(), cache_config,
            dep.comm_signal, store_var_ptr->get_mask());
    }
    ccu_v2::set_cke(instr++, 0, 0, dep.comm_signal, store_var_ptr->get_mask(), 1);

    return HcclResult::HCCL_SUCCESS;
}

// halfRtt
HcclResult ccu_ins_generater_v2::ccu_rep_write_var_atomic_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_write_var_atomic* write_var_atom_ptr,
    const trans_dep& dep)
{
    CHK_PTR_NULL(write_var_atom_ptr);
    (void)ccu_kernel;
    (void)cur_instr_id;
    (void)dep;

    ccu_v2::sync_at_x(
        instr++, write_var_atom_ptr->get_var_addr_id(), write_var_atom_ptr->get_var_token_id(),
        write_var_atom_ptr->get_target_id(), write_var_atom_ptr->get_channel_var_id(), write_var_atom_ptr->get_sem_id(),
        write_var_atom_ptr->get_mask());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_write_with_cnt_inc_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_write_with_cnt_inc* write_cnt_inc_ptr,
    const trans_dep& dep)
{
    CHK_PTR_NULL(write_cnt_inc_ptr);
    (void)ccu_kernel;
    (void)cur_instr_id;

    ccu_v2::trans_mem_notify_info notify{};
    ccu_v2::trans_mem_reduce_info reduce{};
    ccu_v2::trans_mem_config config{};
    config.dma_op_code = URMA_DMA_OP_WRITE_WITH_CNT_INC;
    config.split_mode = SPLIT_MODE_PACKET;
    notify.xn_id = write_cnt_inc_ptr->get_inc_cnt_addr_id();
    notify.xnt_id = write_cnt_inc_ptr->get_inc_cnt_token_id();
    notify.value = 1;

    const uint16_t len_id = write_cnt_inc_ptr->get_len_id();
    const uint16_t len_limit_xn_id = dep.comm_xn[0];
    const uint16_t skip_offset_xn_id = dep.comm_xn[1];
    auto emit_trans_mem = [&]() {
        ccu_v2::trans_mem(
            instr++, write_cnt_inc_ptr->get_rem_addr_id(), write_cnt_inc_ptr->get_rem_token_id(),
            write_cnt_inc_ptr->get_loc_addr_id(), write_cnt_inc_ptr->get_loc_token_id(), len_id,
            write_cnt_inc_ptr->get_channel_var_id(), notify, reduce, config, 0, 0);
    };

    // 超限分支自带一份 TransMem 并以陷阱 Wait 收尾，避免异步 TransMem 报错前 mission 已继续执行。
    ccu_v2::load_imd_to_xn(instr++, len_limit_xn_id, CCU_WRITE_CNT_INC_LEN_LIMIT, 0, 0);
    ccu_v2::load_imd_to_xn(instr++, skip_offset_xn_id, TRANSMEM_LEN_GUARD_SKIP_OFFSET, 0, 0);
    ccu_v2::jump(
        instr++, skip_offset_xn_id, len_id, len_limit_xn_id, static_cast<uint16_t>(condition_type::less_equal));
    ccu_v2::assign(instr++, write_cnt_inc_ptr->get_orig_len_id(), len_id, 0, 0);
    ccu_v2::load_imd_to_xn(instr++, write_cnt_inc_ptr->get_loc_token_id(), CCU_GUARD_TOKEN_INVALID_VALUE, 0, 0);
    ccu_v2::load_imd_to_xn(instr++, len_id, CCU_WRITE_CNT_INC_LEN_LIMIT, 0, 0);
    emit_trans_mem();
    ccu_v2::wait(instr++, len_limit_xn_id, len_limit_xn_id, static_cast<uint16_t>(condition_type::not_equal));

    emit_trans_mem();
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_load_add_imm_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_add_imm* load_add_imm_ptr,
    const trans_dep& dep)
{
    CHK_PTR_NULL(load_add_imm_ptr);
    (void)ccu_kernel;
    (void)cur_instr_id;
    (void)dep;

    ccu_v2::load_x(
        instr++, load_add_imm_ptr->get_dst_id(), load_add_imm_ptr->get_src_id(), load_add_imm_ptr->get_src_offset_id(),
        load_add_imm_ptr->get_imm_add_value(), 0, 0, 0);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_store_add_imm_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_add_imm* store_add_imm_ptr,
    const trans_dep& dep)
{
    CHK_PTR_NULL(store_add_imm_ptr);
    (void)ccu_kernel;
    (void)cur_instr_id;
    (void)dep;

    ccu_v2::store_x(
        instr++, store_add_imm_ptr->get_dst_id(), store_add_imm_ptr->get_src_id(),
        store_add_imm_ptr->get_imm_add_value(), store_add_imm_ptr->get_dst_offset_id(), 0, 0, 0);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::prepare_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel)
{
    CHK_PTR_NULL(rep_ptr);
    CHK_PTR_NULL(ccu_kernel);
    HCCL_INFO("ccu_ins_generater_v2::prepare_const_value Current RepType[%d]", rep_ptr->type());
    switch (rep_ptr->type()) {
        case ccu_rep_type::load:
            return prepare_load_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::load_var:
            return prepare_load_var_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::store:
            return prepare_store_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::store_var:
            return prepare_store_var_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::rem_post_sem:
            return prepare_rem_post_sem_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::rem_post_var:
            return prepare_rem_post_var_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::write:
            return prepare_write_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::read:
            return prepare_read_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::buf_write:
            return prepare_buf_write_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::buf_read:
            return prepare_buf_read_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::local_cpy:
            return prepare_loc_cpy_const_value(rep_ptr, dep, ccu_kernel);
        case ccu_rep_type::record_shared_notify:
            return prepare_record_shared_notify_const_value(rep_ptr, dep, ccu_kernel);
        default:
            break;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::prepare_load_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_load* load_ptr = dynamic_cast<ccu_rep_load*>(rep_ptr);
    CHK_PTR_NULL(load_ptr);
    std::vector<uint64_t> values = {
        load_ptr->get_addr(), dep.mem_token_info, HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_ptr->get_num()};
    std::vector<uint64_t> secret_values = {dep.mem_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_load_var_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_load_var* load_var_ptr = dynamic_cast<ccu_rep_load_var*>(rep_ptr);
    CHK_PTR_NULL(load_var_ptr);
    std::vector<uint64_t> values = {dep.mem_token_info, HCOMM_CCU_RESOURCE_XN_PER_SIZE * load_var_ptr->get_num()};
    std::vector<uint64_t> secret_values = {dep.mem_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_store_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_store* store_ptr = dynamic_cast<ccu_rep_store*>(rep_ptr);
    CHK_PTR_NULL(store_ptr);
    std::vector<uint64_t> values = {
        store_ptr->get_addr(), dep.mem_token_info, HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_ptr->get_num()};
    std::vector<uint64_t> secret_values = {dep.mem_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_store_var_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_store_var* store_var_ptr = dynamic_cast<ccu_rep_store_var*>(rep_ptr);
    CHK_PTR_NULL(store_var_ptr);
    std::vector<uint64_t> values = {HCOMM_CCU_RESOURCE_XN_PER_SIZE * store_var_ptr->get_num()};
    std::vector<uint64_t> secret_values;
    if (!store_var_ptr->get_hscb_flag()) {
        values.push_back(dep.mem_token_info);
        secret_values.push_back(dep.mem_token_info);
    }
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_rem_post_sem_const_value(
    ccu_rep_base* rep_ptr, [[maybe_unused]] const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_rem_post_sem* rep_rem_post_sem_ptr = dynamic_cast<ccu_rep_rem_post_sem*>(rep_ptr);
    CHK_PTR_NULL(rep_rem_post_sem_ptr);

    ccu_channel channel_impl(rep_rem_post_sem_ptr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_rem_post_sem_ptr->get_channel()), HCCL_E_INTERNAL);
    uint64_t rmt_signal_addr{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_addr_by_index(rep_rem_post_sem_ptr->get_sem_index(), rmt_signal_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote signal addr, channelHandle[0x%llx].", __func__,
            rep_rem_post_sem_ptr->get_channel()),
        HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    std::vector<uint64_t> values = {channel_impl->get_channel_id(), rmt_signal_addr, rmt_token};
    std::vector<uint64_t> secret_values = {rmt_token};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_rem_post_var_const_value(
    ccu_rep_base* rep_ptr, [[maybe_unused]] const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_rem_post_var* rep_rem_post_var_ptr = dynamic_cast<ccu_rep_rem_post_var*>(rep_ptr);
    CHK_PTR_NULL(rep_rem_post_var_ptr);

    ccu_channel channel_impl(rep_rem_post_var_ptr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_rem_post_var_ptr->get_channel()), HCCL_E_INTERNAL);
    uint64_t rmt_signal_addr{0};
    uint64_t rmt_var_addr{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_cke_addr_by_index(rep_rem_post_var_ptr->get_sem_index(), rmt_signal_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote signal addr, channelHandle[0x%llx].", __func__,
            rep_rem_post_var_ptr->get_channel()),
        HCCL_E_INTERNAL);
    CHK_PRT_RET(
        channel_impl->get_rmt_var_addr_by_index(rep_rem_post_var_ptr->get_param_index(), rmt_var_addr) !=
            HcclResult::HCCL_SUCCESS,
        HCCL_ERROR(
            "[ccu_ins_generater_v2][%s] failed to get remote var addr, channelHandle[0x%llx].", __func__,
            rep_rem_post_var_ptr->get_channel()),
        HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    std::vector<uint64_t> values = {channel_impl->get_channel_id(), rmt_signal_addr, rmt_token, rmt_var_addr};
    std::vector<uint64_t> secret_values = {rmt_token};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_write_const_value(
    ccu_rep_base* rep_ptr, [[maybe_unused]] const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_write* rep_write_ptr = dynamic_cast<ccu_rep_write*>(rep_ptr);
    CHK_PTR_NULL(rep_write_ptr);

    ccu_channel channel_impl(rep_write_ptr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_write_ptr->get_channel()), HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    std::vector<uint64_t> values = {channel_impl->get_channel_id(), rmt_token};
    std::vector<uint64_t> secret_values = {rmt_token};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_read_const_value(
    ccu_rep_base* rep_ptr, [[maybe_unused]] const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_read* repReadPtr = dynamic_cast<ccu_rep_read*>(rep_ptr);
    CHK_PTR_NULL(repReadPtr);

    ccu_channel channel_impl(repReadPtr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", repReadPtr->get_channel()), HCCL_E_INTERNAL);
    std::vector<uint64_t> values = {channel_impl->get_channel_id()};
    return ccu_kernel->add2_const_value2_var_map(values);
}

HcclResult ccu_ins_generater_v2::prepare_buf_write_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_buf_write* rep_buf_writePtr = dynamic_cast<ccu_rep_buf_write*>(rep_ptr);
    CHK_PTR_NULL(rep_buf_writePtr);

    ccu_channel channel_impl(rep_buf_writePtr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_buf_writePtr->get_channel()), HCCL_E_INTERNAL);
    uint64_t rmt_token{};
    uint32_t rmt_token_id{0};
    uint32_t rmt_token_value{0};
    uint64_t rmt_addr{0};
    uint32_t rmt_size{0};
    CHK_PRT_RET(
        channel_impl->get_rmt_buffer(rmt_addr, rmt_size, rmt_token_id, rmt_token_value) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get remote ccu res buffer"), HCCL_E_UNAVAIL);
    rmt_token = ccu_rep::get_token(rmt_token_id, rmt_token_value, 1);
    std::vector<uint64_t> values = {channel_impl->get_channel_id(), rmt_token, dep.ccu_res_space_token_info};
    std::vector<uint64_t> secret_values = {rmt_token, dep.ccu_res_space_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_buf_read_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_buf_read* rep_buf_readPtr = dynamic_cast<ccu_rep_buf_read*>(rep_ptr);
    CHK_PTR_NULL(rep_buf_readPtr);

    ccu_channel channel_impl(rep_buf_readPtr->get_channel());

    CHK_PRT_RET(
        channel_impl.get_result() != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("failed to get ccu channel, handle[0x%llx]", rep_buf_readPtr->get_channel()), HCCL_E_INTERNAL);
    std::vector<uint64_t> values = {channel_impl->get_channel_id(), dep.ccu_res_space_token_info};
    std::vector<uint64_t> secret_values = {dep.ccu_res_space_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::prepare_loc_cpy_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_loc_cpy* repLocCpyPtr = dynamic_cast<ccu_rep_loc_cpy*>(rep_ptr);
    CHK_PTR_NULL(repLocCpyPtr);
    std::vector<uint64_t> values = {dep.reserve_channal_id[0]};
    return ccu_kernel->add2_const_value2_var_map(values);
}

HcclResult ccu_ins_generater_v2::prepare_record_shared_notify_const_value(
    ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const
{
    ccu_rep_record_shared_notify* sharedNotifyPtr = dynamic_cast<ccu_rep_record_shared_notify*>(rep_ptr);
    CHK_PTR_NULL(sharedNotifyPtr);
    uint64_t ckeAddr = dep.xn_base_addr[sharedNotifyPtr->get_notify().die_id()] + CCU_V2_XN_RESERVE_SIZE +
                       (sharedNotifyPtr->get_notify().id() * CCU_CKE_PER_SIZE);
    std::vector<uint64_t> values = {
        ckeAddr, dep.mem_token_info, CCU_RESOURCE_CKE_MASK_LENGTH, sharedNotifyPtr->get_mask()};
    std::vector<uint64_t> secret_values = {dep.mem_token_info};
    return ccu_kernel->add2_const_value2_var_map(values, secret_values);
}

HcclResult ccu_ins_generater_v2::loop_config_translate(
    ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr, const trans_dep& dep) const
{
    for (const auto& loop : bundle_ptr->get_loops()) {
        ccu_v2::load_imd_to_xn(instr++, loop.ctx_id_var.id(), loop.executor_value.id());
        cur_instr_id++;
        if (loop.layout_value == ccu_rep_loop_group_bundle::layout::config) {
            ccu_v2::load_imd_to_xn(instr++, loop.iter_num_var.id(), loop.config.iter_num);
            cur_instr_id++;
            ccu_v2::load_imd_to_xn(instr++, loop.addr_offset_var.id(), loop.config.addr_offset);
            cur_instr_id++;
        } else if (loop.layout_value == ccu_rep_loop_group_bundle::layout::packed_var) {
            // loop_param_var 布局：iterNum[12:0] gsaOffset[44:13] ctxId[52:45]
            ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], LOOP_ITER_NUM_MASK);
            cur_instr_id++;
            ccu_v2::and_op(instr++, loop.iter_num_var.id(), loop.loop_param_var.id(), dep.comm_xn[0]);
            cur_instr_id++;
            ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], LOOP_GSA_OFFSET_SHIFT);
            cur_instr_id++;
            ccu_v2::srl(instr++, loop.addr_offset_var.id(), loop.loop_param_var.id(), dep.comm_xn[0]);
            cur_instr_id++;
            ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], LOOP_GSA_OFFSET_MASK);
            cur_instr_id++;
            ccu_v2::and_op(instr++, loop.addr_offset_var.id(), loop.addr_offset_var.id(), dep.comm_xn[0]);
            cur_instr_id++;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::loop_group_config_translate(
    ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr, const trans_dep& dep,
    bool isConfig, bool isCompat, uint16_t& loop_group_config_id) const
{
    loop_group_config_id = bundle_ptr->get_parallel_var().id();
    if (isConfig) {
        const auto& cfg = bundle_ptr->get_config();
        uint64_t parallelImm =
            get_parallel_param_v2(cfg.clone_num, bundle_ptr->get_repeat_loop_idx(), bundle_ptr->get_total_loop_num());
        ccu_v2::load_imd_to_xn(instr++, bundle_ptr->get_parallel_var().id(), parallelImm);
        cur_instr_id++;
        uint64_t offset_imm = get_offset_param(cfg.addr_offset, cfg.ccu_buffer_offset, cfg.event_offset);
        ccu_v2::load_imd_to_xn(instr++, bundle_ptr->get_offset_param().id(), offset_imm);
        cur_instr_id++;
        ccu_v2::load_imd_to_xn(instr++, bundle_ptr->get_xn_offset_var().id(), cfg.var_offset);
        cur_instr_id++;
    } else if (isCompat) {
        const uint16_t newXm = bundle_ptr->get_new_parallel_var().id();
        const uint16_t scratch = bundle_ptr->get_scratch_var().id();
        const uint16_t src = bundle_ptr->get_parallel_var().id();
        struct FieldMap {
            uint16_t srcShift;
            uint16_t dstShift;
        };
        const FieldMap fields[] = {{41, 0}, {48, 10}, {55, 19}};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            const uint16_t dst = (i == 0) ? newXm : scratch;
            ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], fields[i].srcShift);
            cur_instr_id++;
            ccu_v2::srl(instr++, dst, src, dep.comm_xn[0]);
            cur_instr_id++;
            ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], LOOP_FIELD_MASK);
            cur_instr_id++;
            ccu_v2::and_op(instr++, dst, dst, dep.comm_xn[0]);
            cur_instr_id++;
            if (fields[i].dstShift != 0) {
                ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], fields[i].dstShift);
                cur_instr_id++;
                ccu_v2::sll(instr++, dst, dst, dep.comm_xn[0]);
                cur_instr_id++;
                ccu_v2::or_op(instr++, newXm, newXm, scratch);
                cur_instr_id++;
            }
        }
        loop_group_config_id = newXm;
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_ins_generater_v2::ccu_rep_loop_group_bundle_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_group_bundle* bundle_ptr,
    const trans_dep& dep)
{
    UNUSED(ccu_kernel);
    CHK_PTR_NULL(bundle_ptr);
    const auto& loops = bundle_ptr->get_loops();
    const auto layout = bundle_ptr->get_layout();
    // 兼容路径：V2 下需插入位重排指令
    const bool isCompat = (layout == ccu_rep_loop_group_bundle::layout::packed_var);
    const bool isConfig = (layout == ccu_rep_loop_group_bundle::layout::config);

    // 每 loop 寄存器：按各 loop 自身构造方式载入，与 group 的构造方式相互独立
    CHK_RET(loop_config_translate(instr, cur_instr_id, bundle_ptr, dep));

    // loopGroupConfig：config 按 960 位分布打包立即数；兼容路径把旧 parallelVar 重排到新分布
    // 新分布：LoopNum[9:0] RepeatLoopIndex[18:10] ExtendNum[27:19]
    // 旧分布：totalLoopNum<<41 repeatLoopIndex<<48 repeatNum<<55
    uint16_t loop_group_config_id{};
    CHK_RET(
        loop_group_config_translate(instr, cur_instr_id, bundle_ptr, dep, isConfig, isCompat, loop_group_config_id));

    // LoopGroup：loops 定义在其后第 3 条起（跳过 2 条无条件跳转）
    // xn_offset(Xp)：config/version960 有来源，兼容路径无来源填 0
    constexpr uint16_t kLoopEntryInstrOffset = 3;
    const uint16_t xn_offset_id = isCompat ? dep.reserve_xn_id : bundle_ptr->get_xn_offset_var().id();
    ccu_v2::loop_group(
        instr++, cur_instr_id + kLoopEntryInstrOffset, loop_group_config_id, bundle_ptr->get_offset_param().id(),
        xn_offset_id); // 向后3条为loop入口
    cur_instr_id++;

    // 无条件跳转，跳过后续 loop 定义
    const uint16_t loop_count = static_cast<uint16_t>(loops.size());
    const uint16_t jumpInstrId = cur_instr_id + 1;
    const uint16_t target_instr_id = cur_instr_id + 2 + loop_count; // 跳转目的为loop指令后额外2条
    ccu_v2::load_imd_to_xn(instr++, dep.comm_xn[0], get_relative_instr_id(jumpInstrId, target_instr_id));
    cur_instr_id++;
    ccu_v2::jump(
        instr++, dep.comm_xn[0], 0, 0,
        static_cast<uint16_t>(CCU_CONDITION_TYPE_DEFAULT) /* hcomm ConditionType::DEFAULT */);
    cur_instr_id++;

    for (const auto& loop : loops) {
        const auto& block = loop.rep_loop_block;
        CHK_PTR_NULL(block);
        ccu_v2::loop(
            instr++, block->start_instr_id(), block->start_instr_id() + block->instr_count() - 1,
            loop.iter_num_var.id(), loop.addr_offset_var.id(), loop.ctx_id_var.id());
        cur_instr_id++;
    }

    ccu_v2::nop(instr++);
    cur_instr_id++;

    return HcclResult::HCCL_SUCCESS;
}

uint16_t ccu_ins_generater_v2::ccu_rep_loop_group_bundle_instr_count(const ccu_rep_loop_group_bundle* bundle_ptr) const
{
    if (bundle_ptr == nullptr) {
        asc::throw_ccu_internal("[%s] bundle_ptr is nullptr", __func__);
    }
    uint16_t total = 0;
    // 每 loop 按自身构造方式计条数
    constexpr uint16_t kConfigLoopInstrCount = 4;       // 3 条载入(ctxId/iterNum/gsaOffset) + 1 条 Loop
    constexpr uint16_t kVersionV2LoopInstrCount = 2;    // 1 条 ctxId 载入 + 1 条 Loop
    constexpr uint16_t kPackedVarLoopInstrCount = 8;    // 7 条(ctxId1 + 位重排6) + 1 条 Loop
    constexpr uint16_t kConfigGroupHeaderCount = 7;     // 打包3 + loopgroup1 + 跳过2 + 收尾1
    constexpr uint16_t kVersionV2GroupHeaderCount = 4;  // loopgroup1 + 跳过2 + 收尾1
    constexpr uint16_t kPackedVarGroupHeaderCount = 22; // 位重排18 + loopgroup1 + 跳过2 + 收尾1
    for (const auto& loop : bundle_ptr->get_loops()) {
        switch (loop.layout_value) {
            case ccu_rep_loop_group_bundle::layout::config:
                total += kConfigLoopInstrCount;
                break;
            case ccu_rep_loop_group_bundle::layout::version_v2:
                total += kVersionV2LoopInstrCount;
                break;
            case ccu_rep_loop_group_bundle::layout::packed_var:
            default:
                total += kPackedVarLoopInstrCount;
                break;
        }
    }
    // group 头按 group 自身构造方式计条数
    switch (bundle_ptr->get_layout()) {
        case ccu_rep_loop_group_bundle::layout::config:
            total += kConfigGroupHeaderCount;
            break;
        case ccu_rep_loop_group_bundle::layout::version_v2:
            total += kVersionV2GroupHeaderCount;
            break;
        case ccu_rep_loop_group_bundle::layout::packed_var:
        default:
            total += kPackedVarGroupHeaderCount;
            break;
    }
    return total;
}

} // namespace ccu_rep
} // namespace asc
