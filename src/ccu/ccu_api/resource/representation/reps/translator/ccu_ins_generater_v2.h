/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CCU_INS_GENERATER_V2
#define CCU_INS_GENERATER_V2
#include "ccu_api/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include <unordered_map>

namespace asc {
namespace ccu_rep {

class ccu_ins_generater_v2 : public ccu_ins_generater_base {
public:
    ccu_ins_generater_v2() {}

    // 虚析构函数，确保派生类对象正确析构
    ~ccu_ins_generater_v2() override = default;

    // data
    HcclResult ccu_rep_buf_loc_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_read* rep_buf_loc_read,
        const trans_dep& dep) override;
    HcclResult ccu_rep_buf_loc_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_loc_write* rep_buf_loc_write,
        const trans_dep& dep) override;
    HcclResult ccu_rep_buf_read_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_read* rep_buf_read, const trans_dep& dep) override;
    HcclResult ccu_rep_buf_reduce_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_reduce* ccu_rep_buf_reduce) override;
    HcclResult ccu_rep_buf_write_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_buf_write* ccu_rep_buf_write, const trans_dep& dep) override;
    HcclResult ccu_rep_loc_cpy_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_cpy* ccu_rep_loc_cpy, const trans_dep& dep) override;
    HcclResult ccu_rep_read_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_read* rep_rem_mem) override;
    HcclResult ccu_rep_rem_mem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_mem* rep_rem_mem) override;
    HcclResult ccu_rep_write_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_write* rep_write) override;
    // sync
    HcclResult ccu_rep_loc_record_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_record_event* ccu_rep_loc_record_event) override;
    HcclResult ccu_rep_loc_wait_event_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_event* ccu_rep_loc_wait_event) override;
    HcclResult ccu_rep_loc_wait_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_loc_wait_notify* ccu_rep_loc_wait_notify) override;
    HcclResult ccu_rep_record_shared_notify_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_record_shared_notify* ccu_rep_record_shared_notify,
        const trans_dep& dep) override;
    HcclResult ccu_rep_rem_wait_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_wait_sem* ccu_rep_rem_wait_sem) override;
    HcclResult ccu_rep_rem_post_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_var* ccu_rep_rem_post_var) override;
    HcclResult ccu_rep_rem_post_sem_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_rem_post_sem* ccu_rep_rem_post_sem,
        const trans_dep& dep) override;
    // logical
    HcclResult ccu_rep_and_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* ccu_rep_and, const trans_dep& dep) override;
    HcclResult ccu_rep_not_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* ccu_rep_not, const trans_dep& dep) override;
    HcclResult ccu_rep_or_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* ccu_rep_or, const trans_dep& dep) override;
    HcclResult ccu_rep_xor_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* ccu_rep_xor, const trans_dep& dep) override;
    // shift
    HcclResult ccu_rep_sh_l_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* ccu_rep_sh_l, const trans_dep& dep) override;
    HcclResult ccu_rep_sh_r_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* ccu_rep_sh_r, const trans_dep& dep) override;
    // arithmetic
    HcclResult ccu_rep_add_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_add* ccu_rep_add, const trans_dep& dep) override;
    HcclResult ccu_rep_assign_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_assign* ccu_rep_assign, const trans_dep& dep) override;
    HcclResult ccu_rep_mul_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_mul* ccu_rep_mul) override;
    HcclResult ccu_rep_sub_translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sub* ccu_rep_sub) override;
    // control
    HcclResult ccu_rep_func_block_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_block* func_block_ptr,
        const trans_dep& dep, uint32_t step) override;
    HcclResult ccu_rep_func_call_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_func_call* func_call_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump* jump_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_ne_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ne* jump_ne_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_eq_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_eq* jump_eq_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_le_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_le* jump_le_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_ge_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_ge* jump_ge_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_gt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_gt* jump_gt_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_jump_lt_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_jump_lt* jump_lt_ptr,
        const trans_dep& dep) override;
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
    HcclResult ccu_rep_nop_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_nop* nop_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_store_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store* store_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_store_var_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_var* store_var_ptr,
        const trans_dep& dep) override;
    // 0.5rtt（V2）
    HcclResult ccu_rep_write_var_atomic_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_write_var_atomic* write_var_atom_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_write_with_cnt_inc_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id,
        ccu_rep_write_with_cnt_inc* write_cnt_inc_ptr, const trans_dep& dep) override;
    HcclResult ccu_rep_load_add_imm_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_add_imm* load_add_imm_ptr,
        const trans_dep& dep) override;
    HcclResult ccu_rep_store_add_imm_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_add_imm* store_add_imm_ptr,
        const trans_dep& dep) override;

    uint32_t get_instr_count(ccu_rep_type rep_type) override;
    HcclResult prepare_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) override;

private:
    HcclResult ccu_rep_jump_translate_v2_base(
        const ccu_instr* const& cur_instr, const uint16_t& cur_instr_id, ccu_rep_jump_base* jump_base_ptr,
        uint64_t expected, const variable& condition, const variable& expected_var, condition_type cond_type) const;

    void load_func_call_in_args(
        ccu_instr* instr, std::vector<ccu_rep_arg>& in_args, std::vector<variable>& formal_ins,
        uint16_t reserve_xn_id) const;
    void load_func_call_out_args(
        ccu_instr* instr, uint32_t offset, std::vector<ccu_rep_arg>& out_args, ccu_rep_reference_manager* func_manager,
        uint16_t reserve_xn_id) const;
    HcclResult load_loop_call_arg(ccu_instr*& instr, const ccu_rep_arg& in_arg, const ccu_rep_arg& blk_arg) const;
    HcclResult prepare_load_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_load_var_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_store_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_store_var_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_rem_post_sem_const_value(
        ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_rem_post_var_const_value(
        ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_write_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_read_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_buf_write_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_buf_read_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_loc_cpy_const_value(ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult prepare_record_shared_notify_const_value(
        ccu_rep_base* rep_ptr, const trans_dep& dep, ccu_kernel* ccu_kernel) const;
    HcclResult loop_config_translate(
        ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr,
        const trans_dep& dep) const;
    HcclResult loop_group_config_translate(
        ccu_instr*& instr, uint16_t& cur_instr_id, const ccu_rep_loop_group_bundle* bundle_ptr, const trans_dep& dep,
        bool is_config, bool is_compat, uint16_t& loop_group_config_id) const;

    static constexpr uint32_t V2_FUNC_BLOCK_INSTR_NUM = 11; // FuncBlock: RelJmp(9)+Jump(1)+Nop(1)
    static constexpr uint32_t V2_FUNC_CALL_INSTR_NUM = 13;  // FuncCall: FuncBlock(11)+Call(2)

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
        {ccu_rep_type::assign, 1},
        {ccu_rep_type::add, 1},
        {ccu_rep_type::mul, 1},
        {ccu_rep_type::sub, 1},
        {ccu_rep_type::loc_record_event, 1},
        {ccu_rep_type::loc_wait_event, 1},
        {ccu_rep_type::loc_wait_notify, 1},
        {ccu_rep_type::record_shared_notify, 1},
        {ccu_rep_type::rem_post_sem, 1},
        {ccu_rep_type::rem_post_var, 1},
        {ccu_rep_type::rem_wait_sem, 1},
        {ccu_rep_type::func_block, V2_FUNC_BLOCK_INSTR_NUM},
        {ccu_rep_type::func_call, V2_FUNC_CALL_INSTR_NUM},
        {ccu_rep_type::jump, 2},
        {ccu_rep_type::jump_ne, 3},
        {ccu_rep_type::jump_eq, 3},
        {ccu_rep_type::loop, 1},
        {ccu_rep_type::loop_group, 1},
        {ccu_rep_type::set_loop, 1},
        {ccu_rep_type::load, 3},
        {ccu_rep_type::load_var, 3},
        {ccu_rep_type::load_arg, 1},
        {ccu_rep_type::store, 3},
        {ccu_rep_type::store_var, 3},
        {ccu_rep_type::write_var_atomic, 1},
        {ccu_rep_type::write_with_cnt_inc, 9},
        {ccu_rep_type::load_add_imm, 1},
        {ccu_rep_type::store_add_imm, 1},
        {ccu_rep_type::and_op, 1},
        {ccu_rep_type::not_op, 1},
        {ccu_rep_type::or_op, 1},
        {ccu_rep_type::xor_op, 1},
        {ccu_rep_type::shl, 1},
        {ccu_rep_type::shr, 1},
        {ccu_rep_type::nop, 1},
    };
};

uint32_t get_relative_instr_id(uint32_t current_instr_id, uint32_t target_instr_id);

} // namespace ccu_rep
} // namespace asc

#endif // CCU_INS_GENERATER_V2
