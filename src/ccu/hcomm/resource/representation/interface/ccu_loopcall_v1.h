/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LOOP_CALL_H
#define CCU_LOOP_CALL_H

#include "hcomm/resource/representation/reps/loop/ccu_rep_loopcall_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {

class loop_call {
public:
    loop_call(ccu_rep_context* context, const std::string& label);
    const std::string& get_label() const { return label_; }

    template <typename... arguments>
    loop_call& operator()(const arguments&... args)
    {
        set_arg_helper(args...);
        append_to_context();
        return *this;
    }

    loop_call& operator()()
    {
        append_to_context();
        return *this;
    }

    void append_to_context();

private:
    template <typename first_t>
    void set_arg_helper(const first_t& first)
    {
        rep_loop_call_->set_in_arg(first);
    }

    template <typename first_t, typename... rest_t>
    void set_arg_helper(const first_t& first, const rest_t&... rest)
    {
        rep_loop_call_->set_in_arg(first);
        set_arg_helper(rest...);
    }

    ccu_rep_context* context_{nullptr};
    std::string label_;

    std::shared_ptr<ccu_rep_loop_call> rep_loop_call_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_LOOP_CALL_H
