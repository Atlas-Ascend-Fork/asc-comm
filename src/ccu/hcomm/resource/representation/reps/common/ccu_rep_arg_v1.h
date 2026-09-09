/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_ARG_H
#define ASCCOMM_CCU_REPRESENTATION_ARG_H

#include <vector>
#include <memory>

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

enum class ccu_arg_type {
    variable,
    memory,
    variable_list,
    memory_list,
    local_addr,      // 1. 新增枚举值
    local_addr_list, // 2. List 类型，以防后面需要 vector<LocalAddr>
    remote_addr,
    remote_addr_list,
};

struct ccu_rep_arg {
    explicit ccu_rep_arg(const variable& var) : type(ccu_arg_type::variable), var(var) {}
    explicit ccu_rep_arg(const memory& mem) : type(ccu_arg_type::memory), mem(mem) {}
    explicit ccu_rep_arg(const std::vector<variable>& var_list) : type(ccu_arg_type::variable_list), var_list(var_list)
    {}
    explicit ccu_rep_arg(const std::vector<memory>& mem_list) : type(ccu_arg_type::memory_list), mem_list(mem_list) {}
    // 新增：LocalAddr 的构造函数
    explicit ccu_rep_arg(const local_addr& addr) : type(ccu_arg_type::local_addr), local_addr_value(addr) {}
    // 新增：LocalAddr 列表的构造函数
    explicit ccu_rep_arg(const std::vector<local_addr>& addr_list)
        : type(ccu_arg_type::local_addr_list), local_addr_list(addr_list)
    {}
    explicit ccu_rep_arg(const remote_addr& addr) : type(ccu_arg_type::remote_addr), remote_addr_value(addr) {}
    explicit ccu_rep_arg(const std::vector<remote_addr>& addr_list)
        : type(ccu_arg_type::remote_addr_list), remote_addr_list(addr_list)
    {}

    ccu_arg_type type;
    variable var;
    memory mem;
    std::vector<variable> var_list;
    std::vector<memory> mem_list;
    local_addr local_addr_value;             // 3. 新增成员变量来存储
    std::vector<local_addr> local_addr_list; // 新增成员变量
    remote_addr remote_addr_value;
    std::vector<remote_addr> remote_addr_list;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_ARG_H
