/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LOOP_H
#define CCU_LOOP_H

#include <cstdint>
#include <vector>

#include "ccu/ccu_api/ccu_types.h"

namespace asc {
namespace ccu {

class variable;
class func;

class loop {
public:
    loop(variable& loop_cfg, const func& func);

    loop(const ccu_loop_config& loop_cfg, const func& func);

    ccu_loop handle() const { return handle_; }

    bool is_var_based() const { return is_var_based_; }

    variable* loop_param_var() const { return loop_param_var_; }

    const ccu_loop_config* config() const { return &config_; }

private:
    void compose_loop_body(const func& func);

    ccu_loop handle_{0};
    bool is_var_based_{false};
    variable* loop_param_var_{nullptr};
    ccu_loop_config config_{};
};

class loop_group {
public:
    loop_group(variable& parallel_cfg, variable& offset_cfg, uint32_t max_loop_num, const std::vector<loop>& loops);

    loop_group(const ccu_loop_group_config& loop_group_cfg, uint32_t max_loop_num, const std::vector<loop>& loops);

    ccu_loop_group handle() const { return handle_; }

private:
    static ccu_loop_cfg to_cfg(const ccu_loop_config& config);

    static ccu_loop_group_cfg to_cfg(const ccu_loop_group_config& config);

    void add_loops(const std::vector<loop>& loops);

    ccu_loop_group handle_{0};
};

} // namespace ccu
} // namespace asc

#endif // CCU_LOOP_H
