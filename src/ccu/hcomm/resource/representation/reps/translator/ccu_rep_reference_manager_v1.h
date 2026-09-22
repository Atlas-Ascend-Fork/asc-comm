/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REP_REFERENCE_MANAGER_H
#define CCU_REP_REFERENCE_MANAGER_H

#include <unordered_map>
#include <vector>
#include <memory>

#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"

namespace asc {
namespace ccu_rep {

// 支持自定义算子CCU开发资源管理优化，减少预留资源数量，避免xn耗尽。
// FUNC_ARG_MAX：单个 ccu::Func 入参/出参上限。每提升 1，每个 die 在
// CcuRepReferenceManager::GetRes 中多使用 2 个 Xn（funcInVar + funcOutVar）。
constexpr uint16_t func_arg_max = 1;
constexpr uint16_t func_nest_max = 1;
constexpr uint16_t func_call_layer_invalid = 0xFFFF;
constexpr uint16_t ccu_reference_manager_xn_num = func_arg_max + func_arg_max + 1 + func_nest_max + 1;

static_assert(ccu_reference_manager_xn_num == 5, "reference manager XN count changed");

class ccu_rep_reference_manager {
public:
    explicit ccu_rep_reference_manager(uint8_t dei_id);
    void get_res(ccu_rep_resource& res);
    std::shared_ptr<ccu_rep_block> get_ref_block(const std::string& label);
    void set_ref_block(const std::string& label, std::shared_ptr<ccu_rep_block> ref_block);
    uint16_t get_func_addr(const std::string& label);
    const variable& get_func_call();
    const variable& get_func_ret(uint16_t call_layer);
    const std::vector<variable>& get_func_in();
    const std::vector<variable>& get_func_out();
    void dump() const;
    void clear_rep_reference();

private:
    bool check_valid(const std::string& label);
    bool check_unique(const std::string& label);

private:
    uint8_t die_id_{0};
    std::unordered_map<std::string, std::shared_ptr<ccu_rep_block>> reference_map_;
    std::vector<variable> func_call_var_;
    std::vector<variable> func_in_var_;
    std::vector<variable> func_out_var_;
};

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_REP_REFERENCE_MANAGER_H
