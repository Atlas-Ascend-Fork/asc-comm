/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CCU_INS_GENERATER_V1
#define CCU_INS_GENERATER_V1

#include <iostream>
#include <string>
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
namespace asc {
namespace ccu_rep {

class ccu_ins_generater_v1 : public ccu_ins_generater_base {
public:
    ccu_ins_generater_v1() {}

    // 虚析构函数，确保派生类对象正确析构
    virtual ~ccu_ins_generater_v1() override = default;

    // data
    HcclResult ccu_rep_loc_cpy_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_cpy* ccu_rep_loc_cpy, const trans_dep& dep) override;
    HcclResult ccu_rep_read_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_read* rep_rem_mem) override;
    HcclResult ccu_rep_rem_mem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_mem* rep_rem_mem) override;
    HcclResult ccu_rep_write_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_write* rep_write) override;
    HcclResult ccu_rep_buf_loc_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_read* rep_buf_loc_read,
        const trans_dep& dep) override;
    HcclResult ccu_rep_buf_loc_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_write* rep_buf_loc_write,
        const trans_dep& dep) override;
    HcclResult ccu_rep_buf_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_write* ccu_rep_buf_write, const trans_dep& dep) override;
    HcclResult ccu_rep_buf_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_read* rep_buf_read, const trans_dep& dep) override;
    HcclResult ccu_rep_buf_reduce_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_reduce* ccu_rep_buf_reduce) override;

    // sync
    HcclResult ccu_rep_record_shared_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_record_shared_notify* ccu_rep_record_shared_notify,
        const trans_dep& dep) override;
    HcclResult ccu_rep_rem_post_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_var* ccu_rep_rem_post_var) override;
    HcclResult ccu_rep_rem_post_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_sem* ccu_rep_rem_post_sem,
        const trans_dep& dep) override;
    HcclResult ccu_rep_rem_wait_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_wait_sem* ccu_rep_rem_wait_sem) override;
    HcclResult ccu_rep_loc_record_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_record_event* ccu_rep_loc_record_event) override;
    HcclResult ccu_rep_loc_wait_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_event* ccu_rep_loc_wait_event) override;
    HcclResult ccu_rep_loc_wait_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_notify* ccu_rep_loc_wait_notify) override;

    // logical
    HcclResult ccu_rep_or_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* ccu_rep_or, const trans_dep& dep) override;
    HcclResult ccu_rep_xor_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* ccu_rep_xor, const trans_dep& dep) override;
    HcclResult ccu_rep_and_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* ccu_rep_and, const trans_dep& dep) override;
    HcclResult ccu_rep_not_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* ccu_rep_not, const trans_dep& dep) override;

    // arithmetic
    HcclResult ccu_rep_add_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_add* ccu_rep_add, const trans_dep& dep) override;
    HcclResult ccu_rep_assign_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_assign* ccu_rep_assign, const trans_dep& dep) override;
    HcclResult ccu_rep_mul_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_mul* ccu_rep_mul) override;
    HcclResult ccu_rep_sub_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sub* ccu_rep_sub) override;

    // shift
    HcclResult ccu_rep_sh_l_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* ccu_rep_sh_l, const trans_dep& dep) override;
    HcclResult ccu_rep_sh_r_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* ccu_rep_sh_r, const trans_dep& dep) override;

    // loop
    HcclResult ccu_rep_loop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop* loop_ptr) override;
    HcclResult ccu_rep_loop_call_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_call* loop_call_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_set_loop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_set_loop* set_loop_ptr) override;
    HcclResult ccu_rep_loop_group_bundle_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_loop_group_bundle* bundle_ptr,
        const trans_dep& dep) override;
    uint16_t ccu_rep_loop_group_bundle_instr_count(const ccu_rep_loop_group_bundle* bundle_ptr) const override;

    // control
    HcclResult ccu_rep_func_block_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, ccu_rep_func_block* func_block_ptr,
        const trans_dep& dep, uint32_t step) override;
    HcclResult ccu_rep_func_call_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_call* func_call_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump* jump_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_eq_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_eq* jump_eq_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_ne_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ne* jump_ne_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_ge_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ge* jump_ge_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_le_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_le* jump_le_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_lt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_lt* jump_lt_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_gt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_gt* jump_gt_ptr,
        const trans_dep& dep) override;

    // common
    HcclResult ccu_rep_load_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load* load_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_load_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_var* load_var_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_load_arg_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_arg* load_arg_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_store_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store* store_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_store_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_var* store_var_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_nop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_nop* nop_ptr,
        const trans_dep& dep) override;

    uint32_t get_instr_count(ccu_rep_type rep_type) override;

private:
    void load_func_call_in_args(
        ccu_instr* instr, std::vector<ccu_rep_arg>& in_args, std::vector<variable>& formal_ins,
        uint16_t reserve_xn_id) const;
    void load_func_call_out_args(
        ccu_instr* instr, uint32_t offset, std::vector<ccu_rep_arg>& out_args, ccu_rep_reference_manager* func_manager,
        uint16_t reserve_xn_id);
    HcclResult load_loop_call_arg(
        ccu_instr*& instr, const ccu_rep_arg& in_arg, const ccu_rep_arg& blk_arg, const trans_dep& dep) const;
    void load_loop_group_params(
        ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr,
        const trans_dep& dep) const;
    void load_loop_group_bundle_config(
        ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr) const;

    std::unordered_map<ccu_rep_type, uint32_t> rep_type_instr_count_ = {
        {ccu_rep_type::read, 1},
        {ccu_rep_type::write, 1},
        {ccu_rep_type::rem_mem, 2},
        {ccu_rep_type::buf_read, 1},
        {ccu_rep_type::local_cpy, 1},
        {ccu_rep_type::local_reduce, 1},
        {ccu_rep_type::buf_write, 1},
        {ccu_rep_type::buf_reduce, 1},
        {ccu_rep_type::buf_loc_read, 1},
        {ccu_rep_type::buf_loc_write, 1},

        {ccu_rep_type::loc_record_event, 1},
        {ccu_rep_type::loc_wait_event, 1},
        {ccu_rep_type::loc_wait_notify, 1},
        {ccu_rep_type::record_shared_notify, 1},
        {ccu_rep_type::rem_post_sem, 1},
        {ccu_rep_type::rem_post_var, 1},
        {ccu_rep_type::rem_wait_sem, 1},

        {ccu_rep_type::assign, 1},
        {ccu_rep_type::add, 1},

        {ccu_rep_type::func_block, 2},
        {ccu_rep_type::func_call, 4},
        {ccu_rep_type::jump, 2},
        {ccu_rep_type::jump_ne, 2},
        {ccu_rep_type::jump_eq, 5},
        {ccu_rep_type::loop, 1},
        {ccu_rep_type::loop_group, 1},
        {ccu_rep_type::set_loop, 2},
        {ccu_rep_type::load, 7},
        {ccu_rep_type::load_var, 7},
        {ccu_rep_type::load_arg, 1},
        {ccu_rep_type::store, 7},
        {ccu_rep_type::store_var, 7}};
};

} // namespace ccu_rep
} // namespace asc

#endif
