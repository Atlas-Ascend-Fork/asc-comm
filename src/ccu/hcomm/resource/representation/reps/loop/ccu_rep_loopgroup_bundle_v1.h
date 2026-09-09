/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H
#define ASCCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H

#include <vector>
#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loop_group_bundle : public ccu_rep_base {
public:
    enum class layout {
        config,     // 结构体 config 构造
        packed_var, // 旧打包变量构造（兼容路径）
        version_v2, // 960 三变量直传
    };

    struct loop_entry {
        ccu_loop_config config;
        executor executor_value;
        std::shared_ptr<ccu_rep_loop_block> rep_loop_block;
        variable loop_param_var;
        variable iter_num_var;
        variable addr_offset_var;
        variable ctx_id_var;
        layout layout_value{layout::config};
    };

    ccu_rep_loop_group_bundle(
        ccu_ins_generater_base* ins_gen_ptr, const ccu_loop_group_config& config, const variable& parallel_var,
        const variable& offset_var);
    explicit ccu_rep_loop_group_bundle(
        ccu_ins_generater_base* ins_gen_ptr, const variable& parallel_var, const variable& offset_var);

    void add_loop(const loop_entry& entry);
    void set_repeat_loop_idx(uint64_t idx) { repeat_loop_idx_ = idx; }
    void set_total_loop_num(uint64_t num) { total_loop_num_ = num; }
    void set_layout(layout layout) { layout_ = layout; }
    void set_xn_offset_var(const variable& xn_offset_var) { xn_offset_var_ = variable(xn_offset_var); }
    void set_compat_remap_vars(const variable& new_parallel_var, const variable& scratch_var)
    {
        // Variable 的 const& operator= 是 DSL 赋值，须用临时量走移动赋值做纯拷贝
        new_parallel_var_ = variable(new_parallel_var);
        scratch_var_ = variable(scratch_var);
    }

    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    uint16_t instr_count() override;
    std::string describe() override;

    uint16_t get_start_loop_instr_id() const;
    const variable& get_offset_param() const { return offset_var_; }

    const std::vector<loop_entry>& get_loops() const { return loops_; }
    const ccu_loop_group_config& get_config() const { return config_; }
    const variable& get_parallel_var() const { return parallel_var_; }
    uint64_t get_repeat_loop_idx() const { return repeat_loop_idx_; }
    uint64_t get_total_loop_num() const { return total_loop_num_; }
    layout get_layout() const { return layout_; }
    const variable& get_new_parallel_var() const { return new_parallel_var_; }
    const variable& get_scratch_var() const { return scratch_var_; }
    const variable& get_xn_offset_var() const { return xn_offset_var_; }

private:
    uint16_t loop_group_instr_offset_in_bundle() const;

    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    ccu_loop_group_config config_;
    variable parallel_var_;
    variable offset_var_;
    uint64_t repeat_loop_idx_{0};
    uint64_t total_loop_num_{0};
    std::vector<loop_entry> loops_;
    layout layout_{layout::config};
    variable new_parallel_var_;
    variable scratch_var_;
    variable xn_offset_var_;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H
