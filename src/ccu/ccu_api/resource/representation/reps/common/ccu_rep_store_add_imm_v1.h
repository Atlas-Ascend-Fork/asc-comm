/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_STORE_ADD_IMM_H
#define ASCCOMM_CCU_REPRESENTATION_STORE_ADD_IMM_H
#include "ccu_api/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "ccu_api/resource/representation/interface/ccu_datatype_v1.h"
namespace asc {
namespace ccu_rep {
class ccu_rep_store_add_imm : public ccu_rep_base {
public:
    ccu_rep_store_add_imm(
        ccu_ins_generater_base* ins_gen_ptr, variable& dst, uint16_t dst_num, variable& dst_offset,
        uint16_t imm_add_value, variable& src);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t get_dst_id() { return dst_.id(); }
    uint16_t get_dst_offset_id() { return dst_offset_.id(); }
    uint16_t get_src_id() { return src_.id(); }
    uint16_t get_imm_add_value() const { return imm_add_value_; }
    const variable& get_dst() const { return dst_; }
    const variable& get_dst_offset() const { return dst_offset_; }
    const variable& get_src() const { return src_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};
    variable dst_;
    variable dst_offset_;
    variable src_;
    uint16_t imm_add_value_{0};
    uint16_t dst_num_{0};
};
}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_STORE_ADD_IMM_H
