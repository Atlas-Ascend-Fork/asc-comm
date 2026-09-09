/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_BUFREDUCE_H
#define ASCCOMM_CCU_REPRESENTATION_BUFREDUCE_H

#include <vector>

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_buf_reduce : public ccu_rep_base {
public:
    ccu_rep_buf_reduce(
        ccu_ins_generater_base* ins_gen_ptr, const std::vector<ccu_buf>& mem, uint16_t count, uint16_t data_type,
        uint16_t output_data_type, uint16_t op_type, completed_event sem, const ccu_rep::variable& len,
        uint16_t mask = 1);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    const std::vector<ccu_buf>& get_mem() { return mem_; }
    uint16_t get_count() const { return count_; }
    uint16_t get_data_type() const { return data_type_; }
    uint16_t get_output_data_type() const { return output_data_type_; }
    uint16_t get_op_type() const { return op_type_; }
    uint16_t get_xn_length_id() { return xn_id_length_.id(); }
    uint16_t get_mask() const { return mask_; }
    uint16_t get_sem_id() { return sem_.id(); }

    completed_event get_sem() { return sem_; }
    ccu_rep::variable get_xn_id_length() { return xn_id_length_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_;
    std::vector<ccu_buf> mem_;
    uint16_t count_;
    uint16_t data_type_;
    uint16_t output_data_type_;
    uint16_t op_type_;
    completed_event sem_;
    ccu_rep::variable xn_id_length_;

    uint16_t mask_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_BUFREDUCE_H
