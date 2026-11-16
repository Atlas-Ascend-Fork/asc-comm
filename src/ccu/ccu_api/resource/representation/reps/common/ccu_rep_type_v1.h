/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_TYPE_H
#define CCU_REPRESENTATION_TYPE_H

namespace asc {
namespace ccu_rep {

enum class ccu_rep_type {
    base,
    block,
    nop,

    load,
    store,
    load_arg,
    load_var,
    store_var,

    assign,
    add,
    mul,
    sub,
    set_loop,

    and_op,
    or_op,
    xor_op,
    not_op,

    jump,
    jump_ne,
    jump_eq,
    jump_lt,
    jump_le,
    jump_gt,
    jump_ge,
    jump_label,

    func_call,
    func_block,

    loop_call,
    loop,
    loop_group,
    loop_block,
    loop_group_block,

    loc_record_event,
    loc_wait_event,
    loc_wait_notify,
    rem_post_sem,
    rem_wait_sem,
    rem_post_var,
    rem_wait_group,

    read,
    write,
    local_cpy,
    local_reduce,
    rem_mem,

    buf_read,
    buf_write,
    buf_loc_read,
    buf_loc_write,
    buf_reduce,

    shr,
    shl,

    record_shared_notify,

    // 0.5rtt专用
    write_with_arrive_notify,
    clear_all_arrive_notify,
    record_expect_count,
    wait_all_peers_arrive_notify,
};

enum class assign_sub_type { invalid, imd_to_variable, imd_to_addr, var_to_addr, addr_to_addr, var_to_var };

enum class add_sub_type {
    invalid,
    addr_plus_var_to_addr,
    addr_plus_addr_to_addr,
    var_plus_var_to_var,
    self_add_address,
    self_add_variable,
    var_plus_immed_to_var,
    addr_plus_immed_to_addr,
    var_plus_var_to_addr,
    self_add_immed_address,
    self_add_immed_variable,
    var_plus_immed_to_addr,
    addr_plus_immed_to_var,
    addr_plus_addr_to_var
};

enum class mul_sub_type {
    invalid,
    var_mul_var_to_var,
    var_mul_immed_to_var,
    self_mul_var_variable,
    self_mul_immed_variable,
    var_mul_var_to_addr,
    var_mul_addr_to_addr,
    var_mul_immed_to_addr,
    addr_mul_immed_to_addr,
    self_mul_var_address,
    self_mul_immed_address,
    addr_mul_immed_to_var
};

enum class minus_sub_type {
    invalid,
    var_minus_var_to_var,
    var_minus_immed_to_var,
    self_sub_var_variable,
    self_sub_immed_variable,
    addr_minus_var_to_addr,
    addr_minus_immed_to_addr,
    self_sub_var_address,
    self_sub_immed_address,
    var_minus_immed_to_addr,
    addr_minus_immed_to_var
};

enum class and_sub_type {
    invalid,
    var_and_var_to_var,
    self_and_var_variable,
};

enum class or_sub_type { invalid, var_or_var_to_var, self_or_var_variable };

enum class xor_sub_type { invalid, var_xor_var_to_var, self_xor_var_variable };

enum class not_sub_type {
    invalid,
    var_equals_not_var,
};

enum class shift_type { logical_shift, arithmetic_shift, circular_shift, invalid };

enum class shift_sub_type {
    invalid,
    var_equals_var_shift_var,
    var_shift_assign_var,
    addr_equals_var_shift_var,
    addr_shift_assign_var
};

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_REPRESENTATION_TYPE_H
