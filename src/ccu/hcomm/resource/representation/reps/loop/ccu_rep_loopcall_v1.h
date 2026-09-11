/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOOP_CALL_H
#define ASCCOMM_CCU_REPRESENTATION_LOOP_CALL_H

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loop_call : public ccu_rep_base {
public:
    explicit ccu_rep_loop_call(ccu_ins_generater_base* ins_generator_ptr, const std::string& label);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t instr_count() override;
    const std::string& get_label() const;

    void reference(std::shared_ptr<ccu_rep_loop_block> ref_rep);

    void set_in_arg(const variable& var);
    void set_in_arg(const std::vector<variable>& var_list);
    void set_in_arg(const memory& mem);
    void set_in_arg(const std::vector<memory>& mem_list);

    void set_in_arg(const local_addr& addr);
    void set_in_arg(const remote_addr& addr);
    void set_in_arg(const std::vector<local_addr>& addr_list);
    void set_in_arg(const std::vector<remote_addr>& addr_list);

    std::shared_ptr<ccu_rep_loop_block> get_loop_block() { return loop_block_; }
    std::vector<ccu_rep_arg>& get_in_args() { return in_args_; }
    uint32_t get_in_arg_count() const { return in_arg_count_; }

private:
    ccu_ins_generater_base* ins_generator_ptr_;
    std::string label_;
    std::shared_ptr<ccu_rep_loop_block> loop_block_{nullptr};

    std::vector<ccu_rep_arg> in_args_;
    uint32_t in_arg_count_{0};
    uint32_t in_arg_instr_count_{0}; // 处理LoopCall的入参需要的指令数

    ccu_instr* instr_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOOP_CALL_H
