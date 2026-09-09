/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_REMPOSTSEM_H
#define ASCCOMM_CCU_REPRESENTATION_REMPOSTSEM_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_rem_post_sem : public ccu_rep_base {
public:
    explicit ccu_rep_rem_post_sem(
        ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, uint16_t sem_index, uint16_t mask);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint32_t get_id() override;
    uint32_t get_channel_id() const { return channel_id_; }

    ChannelHandle get_channel() const { return channel_; }
    uint16_t get_sem_index() const { return sem_index_; }
    uint16_t get_mask() const { return mask_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    ChannelHandle channel_;
    uint32_t channel_id_{0};
    uint16_t sem_index_{0};
    uint16_t mask_{0};
    uint32_t signal_id_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_REMPOSTSEM_H
