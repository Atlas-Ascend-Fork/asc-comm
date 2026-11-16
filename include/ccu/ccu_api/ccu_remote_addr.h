/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REMOTE_ADDR_H
#define CCU_REMOTE_ADDR_H

#include <type_traits>
#include "ccu/ccu_api/ccu_types.h"
#include "ccu/ccu_api/ccu_variable.h"
#include "ccu/ccu_api/ccu_address.h"

namespace asc {
namespace ccu {

template <typename u>
class array;

class remote_addr final {
public:
    remote_addr();

    remote_addr(const remote_addr& other) : addr(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
    {
        this->handle = other.handle;
        this->addr.handle = other.addr.handle;
        this->token.handle = other.token.handle;
    }
    remote_addr(remote_addr&& other) noexcept : addr(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
    {
        this->handle = other.handle;
        this->addr.handle = other.addr.handle;
        this->token.handle = other.token.handle;
    }
    void operator=(const remote_addr& other)
    {
        this->addr = other.addr;
        this->token = other.token;
    }
    void operator=(remote_addr&& other)
    {
        this->handle = other.handle;
        this->addr.handle = other.addr.handle;
        this->token.handle = other.token.handle;
    }

    address addr;
    variable token;
    ccu_remote_addr_handle handle{0};

private:
    explicit remote_addr(detail::no_alloc_tag) : addr(detail::no_alloc_tag{}), token(detail::no_alloc_tag{}) {}
    template <typename u>
    friend class array;
};

} // namespace ccu
} // namespace asc

#endif // CCU_REMOTE_ADDR_H
