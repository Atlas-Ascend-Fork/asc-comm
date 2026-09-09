/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_JUMP_H
#define CCU_REPRESENTATION_JUMP_H

#include <memory>

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_jumplabel_v1.h"
#include "ccu/hcomm/ccu_api_types.h"

namespace asc {
namespace ccu_rep {

enum class condition_type { equal, not_equal, greater_than, greater_equal, less_than, less_equal, DEFAULT, invalid };

class ccu_rep_jump_base : public ccu_rep_base {
public:
    explicit ccu_rep_jump_base(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id);
    explicit ccu_rep_jump_base(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition);
    void reference(std::shared_ptr<ccu_rep_jump_label> ref_rep);
    void validate_ins_generator_for_jump();

    std::shared_ptr<ccu_rep_jump_label> get_jump_label() { return jump_label_; }

    variable& get_target_instr_id() { return target_instr_id_; }

    variable& get_condition() { return condition_; }

    variable& get_expected_var() { return expected_var_; }

    uint64_t get_expected_num() const { return expected_; }

    ccu_instr* get_instr() { return instr_; }

    bool is_compared_with_immd() const { return comp2_immed_; }

protected:
    CcuResult init_instr(ccu_instr*& instr, uint16_t& instr_id);

    ccu_ins_generater_base* ins_generator_ptr_;
    std::string label_;
    std::shared_ptr<ccu_rep_jump_label> jump_label_{nullptr};
    variable target_instr_id_;
    ccu_instr* instr_{nullptr};

    bool comp2_immed_{false};
    bool support_ccu_v1_{true}; // 暂定用于识别 使用特定的构造方法时是否支持A5的翻译流程

    variable expected_var_;
    variable condition_;
    uint64_t expected_{0};
};

class ccu_rep_jump : public ccu_rep_jump_base {
public:
    explicit ccu_rep_jump(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_ne : public ccu_rep_jump_base {
public:
    ccu_rep_jump_ne(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    ccu_rep_jump_ne(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var); // 仅用于A6翻译
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_eq : public ccu_rep_jump_base {
public:
    ccu_rep_jump_eq(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    ccu_rep_jump_eq(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var); // 仅用于A6翻译
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_le : public ccu_rep_jump_base {
public:
    ccu_rep_jump_le(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var);
    ccu_rep_jump_le(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    bool translate(
        ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& cur_instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_ge : public ccu_rep_jump_base {
public:
    ccu_rep_jump_ge(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var);
    ccu_rep_jump_ge(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    bool translate(
        ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& cur_instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_gt : public ccu_rep_jump_base {
public:
    ccu_rep_jump_gt(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var);
    ccu_rep_jump_gt(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    bool translate(
        ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& cur_instr_id, const trans_dep& dep) override;
    std::string describe() override;
};

class ccu_rep_jump_lt : public ccu_rep_jump_base {
public:
    ccu_rep_jump_lt(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& condition, const variable& expected_var);
    ccu_rep_jump_lt(
        ccu_ins_generater_base* ins_gen_ptr, const std::string& label, const variable& target_instr_id,
        const variable& expected_var, const variable& condition, uint64_t expected);
    bool translate(
        ccu_kernel* ccu_kernel, ccu_instr*& cur_instr, uint16_t& cur_instr_id, const trans_dep& dep) override;
    std::string describe() override;
};
}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_JUMP_H
