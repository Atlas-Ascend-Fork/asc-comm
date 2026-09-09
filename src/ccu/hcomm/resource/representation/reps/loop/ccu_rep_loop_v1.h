/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOOP_H
#define ASCCOMM_CCU_REPRESENTATION_LOOP_H

#include <memory>

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loop : public ccu_rep_base {
public:
    explicit ccu_rep_loop(
        ccu_ins_generater_base* ins_generator_ptr, const std::string& label, const variable& loop_param);
    explicit ccu_rep_loop(
        ccu_ins_generater_base* ins_generator_ptr, const std::string& label, const variable& loop_param,
        const variable& loop_iter_num, const variable& loop_gsa_offset);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    const std::string& get_label() const;

    void reference(std::shared_ptr<ccu_rep_loop_block> ref_rep);
    std::shared_ptr<ccu_rep_base> set_loop_param(executor executor, variable var);
    ccu_rep_loop_block* get_loop_block() { return loop_block_.get(); }

    variable* get_loop_param() { return &loop_param_; }
    variable get_loop_iter_num() { return loop_iter_num_; }
    variable get_loop_gsa_offset() { return loop_gsa_offset_; }

private:
    void validate_ins_generator_for_loop() const;

    ccu_ins_generater_base* ins_generator_ptr_;
    std::string label_;
    std::shared_ptr<ccu_rep_loop_block> loop_block_{nullptr};

    variable loop_param_;
    variable loop_iter_num_;
    variable loop_gsa_offset_;
    ccu_instr* instr_{nullptr};

    bool support_ccu_v1_{false};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_REPRESENTATION_LOOP_H
