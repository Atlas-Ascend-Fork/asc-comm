/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOADARG_H
#define ASCCOMM_CCU_REPRESENTATION_LOADARG_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_load_arg : public ccu_rep_base {
public:
    explicit ccu_rep_load_arg(
        ccu_ins_generater_base* ins_gen_ptr, const variable& var, uint16_t arg_id, uint16_t full_arg_id);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;

    ccu_rep::variable get_var() { return var_; }
    uint16_t get_arg_id() const { return arg_id_; }
    uint16_t get_var_id() const { return var_.id(); }
    uint16_t get_full_arg_id() const { return full_arg_id_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};
    variable var_;
    uint16_t arg_id_{0};
    uint16_t full_arg_id_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_REPRESENTATION_LOADARG_H
