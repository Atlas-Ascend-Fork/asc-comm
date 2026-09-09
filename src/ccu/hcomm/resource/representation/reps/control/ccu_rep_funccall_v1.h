/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_FUNC_CALL_H
#define CCU_REPRESENTATION_FUNC_CALL_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_func_call : public ccu_rep_base {
public:
    explicit ccu_rep_func_call(ccu_ins_generater_base* ins_gen_ptr, const std::string& label);
    explicit ccu_rep_func_call(ccu_ins_generater_base* ins_gen_ptr, const variable& func_addr_var);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t instr_count() override;
    const std::string& get_label() const;

    void reference(std::shared_ptr<ccu_rep_func_block> ref_rep);
    void set_func_manager(ccu_rep_reference_manager* func_manager);

    void set_in_arg(const variable& var);
    void set_out_arg(const variable& var);
    void set_in_arg(const std::vector<variable>& var_list);
    void set_out_arg(const std::vector<variable>& var_list);

    int32_t get_call_layer();

    ccu_rep_reference_manager* get_func_manager() { return func_manager_; }
    std::shared_ptr<ccu_rep_func_block>& get_func_block() { return func_block_; }
    variable get_func_addr_var() { return func_addr_var_; }
    std::vector<ccu_rep_arg>& get_in_args() { return in_args_; }
    std::vector<ccu_rep_arg>& get_out_args() { return out_args_; }
    uint32_t get_in_arg_count() const { return in_arg_count_; }
    uint32_t get_out_arg_count() const { return out_arg_count_; }
    ccu_instr* get_instr() { return instr_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_;
    ccu_rep_reference_manager* func_manager_{nullptr};

    std::string label_;
    std::shared_ptr<ccu_rep_func_block> func_block_{nullptr};
    variable func_addr_var_;

    std::vector<ccu_rep_arg> in_args_;
    std::vector<ccu_rep_arg> out_args_;
    uint32_t in_arg_count_{0};
    uint32_t out_arg_count_{0};

    ccu_instr* instr_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_FUNC_CALL_H
