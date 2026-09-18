/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_BUFWRITE_H
#define ASCCOMM_CCU_REPRESENTATION_BUFWRITE_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_buf_write : public ccu_rep_base {
public:
    ccu_rep_buf_write(
        ccu_ins_generater_base* ins_gen_ptr, const ChannelHandle channel, ccu_buf src, remote_addr dst, variable len,
        completed_event sem, uint16_t mask);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;

    uint16_t get_src_id() { return src_.id(); }
    uint16_t get_dst_addr_id() { return dst_.addr.id(); }
    uint16_t get_dst_token_id() { return dst_.token.id(); }
    uint16_t get_len_id() { return len_.id(); }
    uint16_t get_sem_id() { return sem_.id(); }
    uint32_t get_channel_id() const { return channel_id_; }
    ccu_buf get_src() { return src_; }
    remote_addr get_dst() { return dst_; }
    variable get_len() { return len_; }
    completed_event get_sem() { return sem_; }
    uint16_t get_mask() const { return mask_; }
    ChannelHandle get_channel() const { return channel_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    ChannelHandle channel_;
    uint32_t channel_id_{0};

    ccu_buf src_;
    remote_addr dst_;
    variable len_;

    completed_event sem_;
    uint16_t mask_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_BUFWRITE_H
