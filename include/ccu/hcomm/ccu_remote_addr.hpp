/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REMOTE_ADDR_HPP
#define CCU_REMOTE_ADDR_HPP

#include <type_traits>
#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_variable.hpp"
#include "ccu/hcomm/ccu_address.hpp"

namespace AscendC {
namespace ccu {

template <typename u>
class array;

class remote_addr final {
public:
    remote_addr() : addr_(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_remote_addr_alloc(&this->handle, &this->addr_.handle, &this->token.handle),
            "ccu_remote_addr_alloc: failed");
    }

    remote_addr(const remote_addr& other) : addr_(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
    {
        this->handle = other.handle;
        this->addr_.handle = other.addr_.handle;
        this->token.handle = other.token.handle;
    }
    remote_addr(remote_addr&& other) noexcept : addr_(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
    {
        this->handle = other.handle;
        this->addr_.handle = other.addr_.handle;
        this->token.handle = other.token.handle;
    }
    void operator=(const remote_addr& other)
    {
        this->addr_ = other.addr_;
        this->token = other.token;
    }
    void operator=(remote_addr&& other)
    {
        this->handle = other.handle;
        this->addr_.handle = other.addr_.handle;
        this->token.handle = other.token.handle;
    }

    address addr_;
    variable token;
    ccu_remote_addr_handle handle{0};

private:
    explicit remote_addr(detail::no_alloc_tag) : addr_(detail::no_alloc_tag{}), token(detail::no_alloc_tag{}) {}
    template <typename u>
    friend class array;
};

} // namespace ccu
} // namespace AscendC

#endif // CCU_REMOTE_ADDR_HPP
