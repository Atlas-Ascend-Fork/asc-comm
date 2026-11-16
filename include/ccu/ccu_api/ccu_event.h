/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_EVENT_H
#define CCU_EVENT_H

#include <cstdint>
#include <type_traits>

#include "ccu/ccu_api/ccu_types.h"
#include "ccu/ccu_api/ccu_utils.h"

namespace asc {
namespace ccu {

template <typename u>
class array;

class event final {
public:
    event();

    explicit event(ccu_event_handle acq_handle, uint32_t index = 0);

    event(const event& other) : handle(other.handle) {}

    event(event&& other) noexcept : handle(other.handle) {}

    void operator=(event&& other) { this->handle = other.handle; }

    ccu_event_handle handle{0};

private:
    explicit event(detail::no_alloc_tag) {}
    template <typename u>
    friend class array;
};

} // namespace ccu
} // namespace asc

#endif // CCU_EVENT_H
