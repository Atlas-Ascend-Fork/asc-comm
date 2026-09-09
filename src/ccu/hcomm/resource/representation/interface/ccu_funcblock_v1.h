/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_FUNC_BLOCK_H
#define ASCCOMM_CCU_FUNC_BLOCK_H

#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {

class func_block {
public:
    func_block(ccu_rep_context* context, std::string label, uint16_t call_layer = func_call_layer_invalid);
    ~func_block();

    template <typename... arguments>
    func_block& operator()(const arguments&... args)
    {
        define_in_arg_helper(args...);
        return *this;
    }

    template <typename t>
    void define_in_arg(t&& arg)
    {
        rep_func_block_->define_in_arg(std::forward<t>(arg));
    }

    template <typename t>
    void define_out_arg(t&& arg)
    {
        rep_func_block_->define_out_arg(std::forward<t>(arg));
    }

private:
    template <typename first_t>
    void define_in_arg_helper(const first_t& first)
    {
        rep_func_block_->define_in_arg(first);
    }

    template <typename first_t, typename... rest_t>
    void define_in_arg_helper(const first_t& first, const rest_t&... rest)
    {
        rep_func_block_->define_in_arg(first);
        define_in_arg_helper(rest...);
    }

    ccu_rep_context* context_{nullptr};
    std::string label_;

    std::shared_ptr<ccu_rep_func_block> rep_func_block_{nullptr};
    std::shared_ptr<ccu_rep_block> cur_active_block_{nullptr};

    uint16_t call_layer_{func_call_layer_invalid};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // HCCL_CCU_FUNC_BLOCK_H
