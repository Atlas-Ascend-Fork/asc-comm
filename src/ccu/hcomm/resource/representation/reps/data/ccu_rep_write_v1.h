/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_WRITE_H
#define ASCCOMM_CCU_REPRESENTATION_WRITE_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_write : public ccu_rep_base {
public:
    ccu_rep_write(
        ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, remote_addr rem, local_addr loc, variable len,
        completed_event sem, uint16_t mask);
    ccu_rep_write(
        ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, remote_addr rem, local_addr loc, variable len,
        uint16_t data_type, uint16_t op_type, completed_event sem, uint16_t mask);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;

    uint16_t get_loc_addr_id() { return loc_.addr.id(); }
    uint16_t get_loc_token_id() { return loc_.token.id(); }
    uint16_t get_rem_addr_id() { return rem_.addr.id(); }
    uint16_t get_rem_token_id() { return rem_.token.id(); }
    uint16_t get_len_id() { return len_.id(); }
    uint16_t get_sem_id() { return sem_.id(); }
    uint32_t get_channel_id() const { return channel_id_; }
    ChannelHandle get_channel() const { return channel_; }
    local_addr get_loc() { return loc_; }
    remote_addr get_rem() { return rem_; }
    variable get_len() { return len_; }
    completed_event get_sem() { return sem_; }
    uint16_t get_mask() const { return mask_; }
    uint16_t get_data_type() const { return data_type_; }
    uint16_t get_op_type() const { return op_type_; }
    uint16_t get_reduce_flag() const { return reduce_flag_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    ChannelHandle channel_;
    uint32_t channel_id_{0};
    remote_addr rem_;
    local_addr loc_;
    variable len_;

    completed_event sem_;
    uint16_t mask_{0};

    uint16_t data_type_{0};
    uint16_t op_type_{0};
    uint16_t reduce_flag_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_WRITE_H
