/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CCU_INS_GENERATER_BASE
#define CCU_INS_GENERATER_BASE

#include <iostream>
#include <string>
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/common/ccu_log.h"

namespace asc {
namespace ccu_rep {

class ccu_ins_generater_base {
public:
    ccu_ins_generater_base() {}

    // 虚析构函数，确保派生类对象正确析构
    virtual ~ccu_ins_generater_base() = default;
    // data
    virtual HcclResult ccu_rep_buf_loc_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_read* rep_buf_loc_read, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_buf_loc_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_write* rep_buf_loc_write, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_buf_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_read* rep_buf_read, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_buf_reduce_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_reduce* ccu_rep_buf_reduce) = 0;
    virtual HcclResult ccu_rep_buf_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_write* ccu_rep_buf_write, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_loc_cpy_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_cpy* ccu_rep_loc_cpy, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_read_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_read* rep_rem_mem) = 0;
    virtual HcclResult ccu_rep_rem_mem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_mem* rep_rem_mem) = 0;
    virtual HcclResult ccu_rep_write_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_write* rep_write) = 0;

    // sync
    virtual HcclResult ccu_rep_loc_record_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_record_event* ccu_rep_loc_record_event) = 0;
    virtual HcclResult ccu_rep_loc_wait_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_event* ccu_rep_loc_wait_event) = 0;
    virtual HcclResult ccu_rep_loc_wait_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_notify* ccu_rep_loc_wait_notify) = 0;
    virtual HcclResult ccu_rep_record_shared_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_record_shared_notify* ccu_rep_record_shared_notify,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_rem_wait_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_wait_sem* ccu_rep_rem_wait_sem) = 0;
    virtual HcclResult ccu_rep_rem_post_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_var* ccu_rep_rem_post_var) = 0;
    virtual HcclResult ccu_rep_rem_post_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_sem* ccu_rep_rem_post_sem,
        const trans_dep& dep) = 0;
    // logical
    virtual HcclResult ccu_rep_and_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* ccu_rep_and, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_not_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* ccu_rep_not, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_or_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* ccu_rep_or, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_xor_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* ccu_rep_xor, const trans_dep& dep) = 0;

    // shift
    virtual HcclResult ccu_rep_sh_l_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* ccu_rep_sh_l, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_sh_r_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* ccu_rep_sh_r, const trans_dep& dep) = 0;

    // arithmetic
    virtual HcclResult ccu_rep_add_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_add* ccu_rep_add, const trans_dep& dep) = 0;
    ;
    virtual HcclResult ccu_rep_assign_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_assign* ccu_rep_assign, const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_mul_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_mul* ccu_rep_mul) = 0;
    virtual HcclResult ccu_rep_sub_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sub* ccu_rep_sub) = 0;

    // control
    virtual HcclResult ccu_rep_func_block_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_block* func_block_ptr,
        const trans_dep& dep, uint32_t step) = 0;
    virtual HcclResult ccu_rep_func_call_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_call* func_call_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump* jump_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_ne_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ne* jump_ne_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_eq_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_eq* jump_eq_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_le_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_le* jump_le_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_ge_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ge* jump_ge_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_gt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_gt* jump_gt_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_jump_lt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_lt* jump_lt_ptr,
        const trans_dep& dep) = 0;

    // loop
    virtual HcclResult ccu_rep_loop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop* loop_ptr) = 0;
    virtual HcclResult ccu_rep_loop_call_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_call* loop_call_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_set_loop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_set_loop* set_loop_ptr) = 0;
    virtual HcclResult ccu_rep_loop_group_bundle_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_group_bundle* bundle_ptr,
        const trans_dep& dep) = 0;
    virtual uint16_t ccu_rep_loop_group_bundle_instr_count(const ccu_rep_loop_group_bundle* bundle_ptr) const = 0;

    // common
    virtual HcclResult ccu_rep_load_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load* load_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_load_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_var* load_var_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_load_arg_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_arg* load_arg_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_nop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_nop* nop_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_store_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store* store_ptr,
        const trans_dep& dep) = 0;
    virtual HcclResult ccu_rep_store_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_var* store_var_ptr,
        const trans_dep& dep) = 0;

    virtual uint32_t get_instr_count(ccu_rep_type rep_type) = 0;

    virtual HcclResult prepare_const_value(const ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel)
    {
        // A5使用基类空实现；A6需要根据repType做不同处理
        (void)rep_ptr;
        (void)dep;
        (void)ccu_kernel;
        return HcclResult::HCCL_SUCCESS;
    }

protected:
    struct func_call_context {
        uint32_t in_arg_count{0};
        ccu_rep_reference_manager* func_manager{nullptr};
        std::shared_ptr<ccu_rep_func_block> func_block;
        ccu_instr* instr{nullptr};
        std::vector<variable> formal_ins;
    };

    HcclResult prepare_func_call_context(ccu_rep_func_call* func_call_ptr, func_call_context& ctx) const
    {
        CHK_PTR_NULL(func_call_ptr);
        ctx.in_arg_count = func_call_ptr->get_in_arg_count();
        ctx.func_manager = func_call_ptr->get_func_manager();
        CHK_PTR_NULL(ctx.func_manager);
        ctx.func_block = func_call_ptr->get_func_block();
        CHK_PTR_NULL(ctx.func_block);
        ctx.instr = func_call_ptr->get_instr();
        CHK_PTR_NULL(ctx.instr);
        ctx.formal_ins = ctx.func_block->get_in_arg_vars();
        if (static_cast<uint32_t>(ctx.formal_ins.size()) != ctx.in_arg_count) {
            HCCL_ERROR(
                "FuncCall arg count mismatch: caller = %u, callee formal = %u", ctx.in_arg_count,
                static_cast<uint32_t>(ctx.formal_ins.size()));
            return HCCL_E_PARA;
        }
        return HcclResult::HCCL_SUCCESS;
    }
};
} // namespace ccu_rep
} // namespace asc

#endif
