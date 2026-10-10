/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LOOP_HPP
#define CCU_LOOP_HPP

#include <vector>

#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_variable.hpp"
#include "ccu/hcomm/ccu_func.hpp"
#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_utils.hpp"

namespace AscendC {
namespace ccu {

class loop {
public:
    loop(variable& loop_cfg, const func& func)
    {
        compose_loop_body(func);
        is_var_based_ = true;
        loop_param_var_ = &loop_cfg;
    }

    loop(const ccu_loop_config& loop_cfg, const func& func)
    {
        compose_loop_body(func);
        is_var_based_ = false;
        config_ = loop_cfg;
    }

    ccu_loop handle() const { return handle_; }

    bool is_var_based() const { return is_var_based_; }

    variable* loop_param_var() const { return loop_param_var_; }

    const ccu_loop_config* config() const { return &config_; }

private:
    void compose_loop_body(const func& func)
    {
        if (func.num_in() != 0) {
            throw ::AscendC::ccu::detail::ccu_exception(
                CcuResult::CCU_E_PARA, "ccu::loop requires a no-argument ccu::func");
        }
        CCU_THROW_IF_FAILED(::asc::ccu_loop_create(&handle_), "ccu_loop_create failed");
        CCU_THROW_IF_FAILED(::asc::ccu_loop_body_enter(handle_), "ccu_loop_body_enter failed");
        try {
            func.run_body(nullptr);
        } catch (...) {
            (void)::asc::ccu_loop_body_exit(handle_);
            throw;
        }
        CCU_THROW_IF_FAILED(::asc::ccu_loop_body_exit(handle_), "ccu_loop_body_exit failed");
    }

    ccu_loop handle_{0};
    bool is_var_based_{false};
    variable* loop_param_var_{nullptr};
    ccu_loop_config config_{};
};

class loop_group {
public:
    loop_group(variable& parallel_cfg, variable& offset_cfg, uint32_t max_loop_num, const std::vector<loop>& loops)
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_loop_group_create_from_var(&handle_, max_loop_num, parallel_cfg.handle, offset_cfg.handle),
            "ccu_loop_group_create_from_var failed");
        add_loops(loops);
    }

    loop_group(const ccu_loop_group_config& loop_group_cfg, uint32_t max_loop_num, const std::vector<loop>& loops)
    {
        ccu_loop_group_config local_cfg = loop_group_cfg;
        CCU_THROW_IF_FAILED(
            ::asc::ccu_loop_group_create(&handle_, max_loop_num, &local_cfg), "ccu_loop_group_create failed");
        add_loops(loops);
    }

    ccu_loop_group handle() const { return handle_; }

private:
    void add_loops(const std::vector<loop>& loops)
    {
        for (const auto& loop : loops) {
            if (loop.is_var_based()) {
                auto* loop_param_var = loop.loop_param_var();
                if (loop_param_var == nullptr) {
                    throw ::AscendC::ccu::detail::ccu_exception(
                        CcuResult::CCU_E_PARA, "ccu::loop var_-based loop has null loop parameter");
                }
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_loop_group_add_loop_from_var(handle_, loop.handle(), loop_param_var->handle),
                    "ccu_loop_group_add_loop_from_var failed");
            } else {
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_loop_group_add_loop(handle_, loop.handle(), loop.config()),
                    "ccu_loop_group_add_loop failed");
            }
        }
    }

    ccu_loop_group handle_{0};
};

} // namespace ccu
} // namespace AscendC

#endif // CCU_LOOP_HPP
