/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_BUFFER_HPP
#define CCU_BUFFER_HPP

#include <cstdint>
#include <type_traits>
#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_utils.hpp"

namespace AscendC {
namespace ccu {

template <typename u>
class array;

class ccu_buffer final {
public:
    ccu_buffer() { CCU_THROW_IF_FAILED(::asc::ccu_buffer_alloc(&this->handle), "ccu_buffer_alloc: failed"); }

    ccu_buffer(const ccu_buffer& other) { this->handle = other.handle; }

    ccu_buffer(ccu_buffer&& other) noexcept { this->handle = other.handle; }

    void operator=(ccu_buffer&& other) { this->handle = other.handle; }

    ccu_buffer_handle handle{0};

private:
    explicit ccu_buffer(detail::no_alloc_tag) {}
    template <typename u>
    friend class array;
};

} // namespace ccu
} // namespace AscendC

#endif // CCU_BUFFER_HPP
