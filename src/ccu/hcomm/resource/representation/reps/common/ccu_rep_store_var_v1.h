/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_STORE_VAR_H_V1
#define ASCCOMM_CCU_REPRESENTATION_STORE_VAR_H_V1

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_store_var : public ccu_rep_base {
public:
    ccu_rep_store_var(
        ccu_ins_generater_base* ins_generator_ptr, const variable& var, const variable& dst, uint32_t num = 1,
        bool hscb_flag = false);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;

    variable get_var() { return var_; }
    variable get_dst() { return dst_; }
    uint32_t get_num() const { return num_; }
    uint16_t get_mask() const { return mask_; }
    bool get_hscb_flag() const { return hscb_flag_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};
    variable var_;
    variable dst_;
    uint32_t num_;
    uint16_t mask_{1};
    bool hscb_flag_{false};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_REPRESENTATION_STORE_VAR_H
