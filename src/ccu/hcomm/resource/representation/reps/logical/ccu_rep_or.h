/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_REPRESENTATION_OR_H
#define HCCL_CCU_REPRESENTATION_OR_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_or : public ccu_rep_base {
public:
    ccu_rep_or(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_a, const variable& var_b);
    ccu_rep_or(ccu_ins_generater_base* ins_gen_ptr, const variable& var_c, const variable& var_b);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& cur_instr_id, const trans_dep& dep) override;

    std::string describe() override;

    variable get_var_a() { return var_a_; }
    variable get_var_b() { return var_b_; }
    variable get_var_c() { return var_c_; }
    or_sub_type get_sub_type() const { return sub_type_; }

private:
    or_sub_type sub_type_{or_sub_type::invalid};

    variable var_a_;
    variable var_b_;
    variable var_c_;

    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_REPRESENTATION_OR_H
