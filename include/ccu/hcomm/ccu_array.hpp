/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ARRAY_HPP
#define CCU_ARRAY_HPP

#include <cstdint>
#include <new>
#include <vector>
#include <string>

#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_utils.hpp"
#include "ccu/hcomm/ccu_variable.hpp"
#include "ccu/hcomm/ccu_event.hpp"
#include "ccu/hcomm/ccu_buffer.hpp"

namespace AscendC {
namespace ccu {

// 主模板未实现：未特化的资源类型实例化 array<t> 时编译失败，
// 当前期次仅 variable / event / ccu_buffer 提供底层 ccu_block_*_alloc C 接口。
template <typename t>
struct ccu_array_traits;

template <>
struct ccu_array_traits<variable> {
    using handle_t = ccu_variable_handle;
    static CcuResult block_alloc(handle_t* h, uint32_t n) { return ::asc::ccu_block_variable_alloc(h, n); }
    static CcuResult create_by_acquire(handle_t acq_handle, uint32_t index, handle_t* h)
    {
        return ::asc::ccu_variable_get_by_index(acq_handle, index, h);
    }
    static void set_handle(variable& v, handle_t h) { v.handle = h; }
};

template <>
struct ccu_array_traits<event> {
    using handle_t = ccu_event_handle;
    static CcuResult block_alloc(handle_t* h, uint32_t n) { return ::asc::ccu_block_event_alloc(h, n); }
    static CcuResult create_by_acquire(handle_t acq_handle, uint32_t index, handle_t* h)
    {
        return ::asc::ccu_event_get_by_index(acq_handle, index, h);
    }
    static void set_handle(event& e, handle_t h) { e.handle = h; }
};

template <>
struct ccu_array_traits<ccu_buffer> {
    using handle_t = ccu_buffer_handle;
    static CcuResult block_alloc(handle_t* h, uint32_t n) { return ::asc::ccu_block_buffer_alloc(h, n); }
    static void set_handle(ccu_buffer& b, handle_t h) { b.handle = h; }
};

// 连续资源容器：在构造时一次性 block_alloc 出 count_ 个底层句柄并填充给占位元素。
// 元素本身通过 no_alloc_tag 私有构造跳过单元 alloc，避免与 block_alloc 双重分配。
template <typename t>
class array final {
public:
    explicit array(uint32_t count_) : count_(count_)
    {
        if (count_ == 0) {
            return;
        }
        using H = typename ccu_array_traits<t>::handle_t;
        std::vector<H> handles(count_);
        elems_ = static_cast<t*>(::operator new(sizeof(t) * count_));
        for (uint32_t i = 0; i < count_; ++i) {
            ::new (static_cast<void*>(&elems_[i])) t(detail::no_alloc_tag{});
        }
        auto ret = ccu_array_traits<t>::block_alloc(handles.data(), count_);
        if (ret != CcuResult::CCU_SUCCESS) {
            for (uint32_t i = 0; i < count_; ++i) {
                elems_[i].~t();
            }
            ::operator delete(elems_);
            elems_ = nullptr;
            count_ = 0;
            throw ::AscendC::ccu::detail::ccu_exception(ret, "array block_alloc: failed");
        }
        for (uint32_t i = 0; i < count_; ++i) {
            ccu_array_traits<t>::set_handle(elems_[i], handles[i]);
        }
    }

    array(typename ccu_array_traits<t>::handle_t acq_handle, uint32_t count_) : count_(count_)
    {
        if (count_ == 0) {
            return;
        }
        using H = typename ccu_array_traits<t>::handle_t;
        elems_ = static_cast<t*>(::operator new(sizeof(t) * count_));
        for (uint32_t i = 0; i < count_; ++i) {
            ::new (static_cast<void*>(&elems_[i])) t(detail::no_alloc_tag{});
        }
        for (uint32_t i = 0; i < count_; ++i) {
            H handle{};
            auto ret = ccu_array_traits<t>::create_by_acquire(acq_handle, i, &handle);
            if (ret != CcuResult::CCU_SUCCESS) {
                std::string errMsg = "array creation failed at index " + std::to_string(i) +
                                     ", requested count_=" + std::to_string(count_) +
                                     "; the acquire handle likely holds fewer resources than count_";
                for (uint32_t j = 0; j < count_; ++j) {
                    elems_[j].~t();
                }
                ::operator delete(elems_);
                elems_ = nullptr;
                count_ = 0;
                throw ::AscendC::ccu::detail::ccu_exception(ret, errMsg.c_str());
            }
            ccu_array_traits<t>::set_handle(elems_[i], handle);
        }
    }

    ~array()
    {
        if (elems_ == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < count_; ++i) {
            elems_[i].~t();
        }
        ::operator delete(elems_);
    }

    array(const array&) = delete;
    array& operator=(const array&) = delete;

    array(array&& other) noexcept : elems_(other.elems_), count_(other.count_)
    {
        other.elems_ = nullptr;
        other.count_ = 0;
    }

    array& operator=(array&& other) noexcept
    {
        if (this != &other) {
            this->~array();
            elems_ = other.elems_;
            count_ = other.count_;
            other.elems_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    t& operator[](uint32_t i) { return elems_[i]; }
    const t& operator[](uint32_t i) const { return elems_[i]; }
    t* data() { return elems_; }
    const t* data() const { return elems_; }
    uint32_t size() const { return count_; }

private:
    t* elems_{nullptr};
    uint32_t count_{0};
};

} // namespace ccu
} // namespace AscendC

#endif // CCU_ARRAY_HPP
