/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_REMPOSTVAR_H
#define ASCCOMM_CCU_REPRESENTATION_REMPOSTVAR_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_rem_post_var : public ccu_rep_base {
public:
    ccu_rep_rem_post_var(
        ccu_ins_generater_base* ins_gen_ptr, variable param, const ChannelHandle channel, uint16_t param_index,
        uint16_t sem_index, uint16_t mask);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint32_t get_rmt_xn_id() const { return rmt_xn_id_; }
    uint32_t get_rmt_cke_id() const { return rmt_cke_id_; }
    uint16_t get_param_index() const { return param_index_; }
    variable get_param() { return param_; }
    uint32_t get_channel_id() const { return channel_id_; }

    ChannelHandle get_channel() const { return channel_; }
    uint16_t get_sem_index() const { return sem_index_; }
    uint16_t get_mask() const { return mask_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    variable param_;
    ChannelHandle channel_;
    uint16_t param_index_{0};
    uint16_t sem_index_{0};
    uint16_t mask_{0};
    uint32_t rmt_xn_id_{0};
    uint32_t rmt_cke_id_{0};
    uint32_t channel_id_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_REMPOSTVAR_H
