/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOCCPY_H
#define ASCCOMM_CCU_REPRESENTATION_LOCCPY_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loc_cpy : public ccu_rep_base {
public:
    ccu_rep_loc_cpy(
        ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len, completed_event sem,
        uint16_t mask);
    ccu_rep_loc_cpy(
        ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len, uint16_t data_type,
        uint16_t op_type, completed_event sem, uint16_t mask);

    // 临时验证
    ccu_rep_loc_cpy(
        ccu_ins_generater_base* ins_gen_ptr, local_addr dst, local_addr src, variable len,
        const std::vector<ccu_buf>& bufs, completed_event sem, uint16_t mask);

    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t get_src_addr_id() { return src_.addr.id(); }
    uint16_t get_src_token_id() { return src_.token.id(); }
    uint16_t get_dst_addr_id() { return dst_.addr.id(); }
    uint16_t get_dst_token_id() { return dst_.token.id(); }
    uint16_t get_len_id() { return len_.id(); }
    uint16_t get_sem_id() { return sem_.id(); }
    uint16_t get_data_type() const { return data_type_; }
    uint16_t get_op_type() const { return op_type_; }
    const std::vector<ccu_buf>& get_bufs() { return bufs_; }

    local_addr get_dst() { return dst_; }
    local_addr get_src() { return src_; }
    variable get_len() { return len_; }
    completed_event get_sem() { return sem_; }
    uint16_t get_mask() const { return mask_; }
    uint16_t get_reduce_flag() const { return reduce_flag_; }
    bool get_use_ccu_buffer() const { return use_ccu_buffer_; }

    uint16_t get_first_buf_id();
    uint16_t get_used_buf_num();

private:
    void validate_ins_generator_for_loc_cpy() const;

    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    local_addr dst_;
    local_addr src_;
    variable len_;

    // 用于A6场景locmem2locmem搬运
    std::vector<ccu_buf> bufs_;

    completed_event sem_;
    uint16_t mask_{0};

    uint16_t data_type_{0};
    uint16_t op_type_{0};
    uint16_t reduce_flag_{0};

    bool use_ccu_buffer_ = false;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOCCPY_H
