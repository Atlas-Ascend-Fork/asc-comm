/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_BUFLOCREAD_H
#define ASCCOMM_CCU_REPRESENTATION_BUFLOCREAD_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_buf_loc_read : public ccu_rep_base {
public:
    ccu_rep_buf_loc_read(
        ccu_ins_generater_base* ins_gen_ptr, local_addr src, ccu_buf dst, variable len, completed_event sem,
        uint16_t mask);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t get_src_addr_id() { return src_.addr.id(); }
    uint16_t get_src_token_id() { return src_.token.id(); }
    uint16_t get_dst_id() { return dst_.id(); }
    uint16_t get_len_id() { return len_.id(); }
    uint16_t get_sem_id() { return sem_.id(); }

    local_addr get_src() { return src_; }
    ccu_buf get_dst() { return dst_; }
    variable get_len() { return len_; }
    completed_event get_sem() { return sem_; }
    uint16_t get_mask() const { return mask_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    local_addr src_;
    ccu_buf dst_;
    variable len_;

    completed_event sem_;
    uint16_t mask_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_BUFLOCREAD_H
