/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_FUNC_BLOCK_H
#define CCU_REPRESENTATION_FUNC_BLOCK_H

#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_arg_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_func_block : public ccu_rep_block {
public:
    explicit ccu_rep_func_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t instr_count() override;

    void set_func_manager(ccu_rep_reference_manager* func_manager);

    void define_in_arg(const variable& var);
    void define_out_arg(const variable& var);
    void define_in_arg(const std::vector<variable>& var_list);
    void define_out_arg(const std::vector<variable>& var_list);

    void set_call_layer(uint16_t call_layer);
    uint16_t get_call_layer() const;
    std::vector<variable> get_in_arg_vars() const;

    ccu_rep_reference_manager* get_func_manager() { return func_manager_; }

    std::vector<ccu_rep_arg>& get_in_args() { return in_args_; }

    std::vector<ccu_rep_arg>& get_out_args() { return out_args_; }

    uint16_t get_call_layer() { return call_layer_; }

private:
    ccu_rep_reference_manager* func_manager_{nullptr};

    std::vector<ccu_rep_arg> in_args_;
    std::vector<ccu_rep_arg> out_args_;
    uint32_t in_arg_count_{0};
    uint32_t out_arg_count_{0};

    uint16_t call_layer_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_FUNC_BLOCK_H
