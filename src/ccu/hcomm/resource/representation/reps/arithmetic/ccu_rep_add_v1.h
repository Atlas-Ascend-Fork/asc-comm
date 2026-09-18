/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_ADD_H
#define ASCCOMM_CCU_REPRESENTATION_ADD_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_add : public ccu_rep_base {
public:
    // support ccuV1 & ccuV2
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const variable& var_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const address& addr_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const variable& var_b);
    explicit ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const variable& offset);
    explicit ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const variable& offset);

    // only support ccuV2
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const uint16_t immed_b);
    explicit ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const variable& var_a, const uint16_t immed_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const address& addr_a, const uint16_t immed_b);
    explicit ccu_rep_add(ccu_ins_generater_base* ins_gen_ptr, const address& addr_a, const uint16_t immed_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const uint16_t immed_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const address& addr_a, const uint16_t immed_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const address& addr_a, const address& addr_b);
    explicit ccu_rep_add(
        ccu_ins_generater_base* ins_gen_ptr, const address& addr_c, const variable& var_a, const variable& var_b);

    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;

    address get_addr_a();
    address get_addr_b();
    address get_addr_c();
    variable get_var_a();
    variable get_var_b();
    variable get_var_c();
    uint16_t get_immed_b() const;
    add_sub_type get_sub_type() const;

private:
    void validate_ins_gen_ptr_for_add() const;
    void set_common_info();

    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    add_sub_type sub_type_{add_sub_type::invalid};

    address addr_a_;
    address addr_b_;
    address addr_c_;

    variable var_a_;
    variable var_b_;
    variable var_c_;

    uint16_t immed_b_{0};

    bool support_ccu_v1_{false};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_REPRESENTATION_ADD_H
