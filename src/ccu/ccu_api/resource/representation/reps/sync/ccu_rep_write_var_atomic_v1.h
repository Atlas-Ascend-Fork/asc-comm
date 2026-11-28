/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_WRITE_VAR_ATOMIC_H
#define ASCCOMM_CCU_REPRESENTATION_WRITE_VAR_ATOMIC_H
#include "ccu_api/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "ccu_api/resource/representation/interface/ccu_datatype_v1.h"
namespace asc {
namespace ccu_rep {
class ccu_rep_write_var_atomic : public ccu_rep_base {
public:
    ccu_rep_write_var_atomic(
        ccu_ins_generater_base* ins_gen_ptr, variable& channel, remote_addr& var_addr, variable& target_value,
        completed_event sem, uint16_t mask = 1);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t get_channel_var_id() { return channel_.id(); }
    uint16_t get_var_addr_id() { return var_addr_.addr.id(); }
    uint16_t get_var_token_id() { return var_addr_.token.id(); }
    uint16_t get_target_id() { return target_value_.id(); }
    uint16_t get_sem_id() { return sem_.id(); }
    const variable& get_channel_var() const { return channel_; }
    const remote_addr& get_var_addr() const { return var_addr_; }
    const variable& get_target() const { return target_value_; }
    completed_event get_sem() { return sem_; }
    uint16_t get_mask() const { return mask_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};
    variable channel_;
    remote_addr var_addr_;
    variable target_value_;
    completed_event sem_;
    uint16_t mask_{1};
};
}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_WRITE_VAR_ATOMIC_H
