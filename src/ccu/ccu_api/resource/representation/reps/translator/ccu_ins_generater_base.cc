/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_api/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {
namespace ccu_rep {

// 0.5rtt 专用 rep 的基类默认实现：本生成器（V1/未覆写路径）不支持该 rep 类型。
// 各实现仅做判空与 NOT_SUPPORT 返回，具体编码由 V2 生成器覆写。

HcclResult ccu_ins_generater_base::ccu_rep_write_var_atomic_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_write_var_atomic* write_var_atom_ptr,
    const trans_dep& dep)
{
    (void)ccu_kernel;
    (void)instr;
    (void)cur_instr_id;
    (void)dep;
    CHK_PTR_NULL(write_var_atom_ptr);
    HCCL_ERROR(
        "[ccu_ins_generater_base][%s] unsupported rep type for this generator: %s", __func__,
        write_var_atom_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_base::ccu_rep_write_with_cnt_inc_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_write_with_cnt_inc* write_cnt_inc_ptr,
    const trans_dep& dep)
{
    (void)ccu_kernel;
    (void)instr;
    (void)cur_instr_id;
    (void)dep;
    CHK_PTR_NULL(write_cnt_inc_ptr);
    HCCL_ERROR(
        "[ccu_ins_generater_base][%s] unsupported rep type for this generator: %s", __func__,
        write_cnt_inc_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_base::ccu_rep_load_add_imm_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_load_add_imm* load_add_imm_ptr,
    const trans_dep& dep)
{
    (void)ccu_kernel;
    (void)instr;
    (void)cur_instr_id;
    (void)dep;
    CHK_PTR_NULL(load_add_imm_ptr);
    HCCL_ERROR(
        "[ccu_ins_generater_base][%s] unsupported rep type for this generator: %s", __func__,
        load_add_imm_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ccu_ins_generater_base::ccu_rep_store_add_imm_translate(
    ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, ccu_rep_store_add_imm* store_add_imm_ptr,
    const trans_dep& dep)
{
    (void)ccu_kernel;
    (void)instr;
    (void)cur_instr_id;
    (void)dep;
    CHK_PTR_NULL(store_add_imm_ptr);
    HCCL_ERROR(
        "[ccu_ins_generater_base][%s] unsupported rep type for this generator: %s", __func__,
        store_add_imm_ptr->describe().c_str());
    return HCCL_E_NOT_SUPPORT;
}

} // namespace ccu_rep
} // namespace asc
