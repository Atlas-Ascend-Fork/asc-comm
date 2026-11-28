/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_WRITE_WITH_CNT_INC_H
#define ASCCOMM_CCU_REPRESENTATION_WRITE_WITH_CNT_INC_H
#include "ccu_api/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "ccu_api/resource/representation/interface/ccu_datatype_v1.h"
namespace asc {
namespace ccu_rep {
class ccu_rep_write_with_cnt_inc : public ccu_rep_base {
public:
    ccu_rep_write_with_cnt_inc(
        ccu_ins_generater_base* ins_gen_ptr, variable& channel, remote_addr& rem, local_addr& loc, variable& len,
        remote_addr& inc_cnt, variable& orig_len);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t get_channel_var_id() { return channel_.id(); }
    uint16_t get_loc_addr_id() { return loc_.addr.id(); }
    uint16_t get_loc_token_id() { return loc_.token.id(); }
    uint16_t get_rem_addr_id() { return rem_.addr.id(); }
    uint16_t get_rem_token_id() { return rem_.token.id(); }
    uint16_t get_len_id() { return len_.id(); }
    uint16_t get_inc_cnt_addr_id() { return inc_cnt_.addr.id(); }
    uint16_t get_inc_cnt_token_id() { return inc_cnt_.token.id(); }
    uint16_t get_orig_len_id() { return orig_len_.id(); }
    const variable& get_channel_var() const { return channel_; }
    const remote_addr& get_rem() const { return rem_; }
    const local_addr& get_loc() const { return loc_; }
    const variable& get_len() const { return len_; }
    const remote_addr& get_inc_cnt() const { return inc_cnt_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};
    variable channel_;
    remote_addr rem_;
    local_addr loc_;
    variable len_;
    remote_addr inc_cnt_;
    variable orig_len_;
};
}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_WRITE_WITH_CNT_INC_H
