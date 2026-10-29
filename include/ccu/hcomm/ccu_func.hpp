/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_FUNC_HPP
#define CCU_FUNC_HPP

#include <cstdint>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_variable.hpp"

namespace AscendC {
namespace ccu {

namespace internal {

template <typename t>
struct functor_traits : functor_traits<decltype(&t::operator())> {};

template <typename C, typename R, typename... A>
struct functor_traits<R (C::*)(A...) const> {
    static constexpr std::size_t arity = sizeof...(A);
    using return_t = R;
    template <std::size_t I>
    struct Arg {
        using type_ = typename std::tuple_element<I, std::tuple<A...>>::type_;
    };
};

template <typename C, typename R, typename... A>
struct functor_traits<R (C::*)(A...)> {
    static constexpr std::size_t arity = sizeof...(A);
    using return_t = R;
};

// ---- 把 lambda 包装成 std::function<void(variable*)>，按 N 展开 ----
template <typename Lambda, std::size_t... Is>
inline std::function<void(variable*)> make_body_impl(Lambda body, std::index_sequence<Is...>)
{
    return [body](variable* formals) { body(formals[Is]...); };
}

template <typename Lambda, std::size_t N>
inline std::function<void(variable*)> make_body(Lambda body)
{
    return make_body_impl(body, std::make_index_sequence<N>{});
}

} // namespace internal

class func {
public:
    // ctor：从 lambda 推导形参个数 N，类型擦除到 body_。
    // 要求 lambda 返回 void、形参全部为 ccu::variable（运行期由 kernel 校验形参数量）。
    template <typename Lambda>
    explicit func(Lambda body)
        : num_in_(internal::functor_traits<Lambda>::arity),
          body_(internal::make_body<Lambda, internal::functor_traits<Lambda>::arity>(body))
    {
        static_assert(
            std::is_same<typename internal::functor_traits<Lambda>::return_t, void>::value,
            "ccu::func: lambda must return void (no return value supported)");
    }

    func(const func&) = delete;
    func& operator=(const func&) = delete;

    uint32_t num_in() const { return num_in_; }
    const void* key() const { return static_cast<const void*>(this); }

    // 在合成模式下调用：把 N 个 formal variable 展开传给原 lambda。
    void run_body(variable* formals) const { body_(formals); }

private:
    uint32_t num_in_{0};
    std::function<void(variable*)> body_;
};

// ccu::call_func：global / static ccu::func 引用 NTTP（C++14 合法）。
// 用法：static ccu::func MyAdd(lambda); ... ccu::call_func<MyAdd>(x, y);
template <func& Obj, typename... args>
inline CcuResult call_func(args... args_)
{
    constexpr std::size_t arg_num = sizeof...(args);
    if (static_cast<uint32_t>(arg_num) != Obj.num_in()) {
        return CcuResult::CCU_E_PARA;
    }

    uint64_t handle = 0;
    CCU_THROW_IF_FAILED(::asc::ccu_func_block_lookup(Obj.key(), &handle), "call_func: ccu_func_block_lookup failed");
    if (handle == 0) {
        // 第一次进入：合成 func_block
        CCU_THROW_IF_FAILED(::asc::ccu_func_block_begin(Obj.key(), &handle), "call_func: ccu_func_block_begin failed");

        // 形参：按 lambda 形参个数 alloc，并注册为 func_block 的入参
        std::vector<variable> formals(Obj.num_in());
        for (uint32_t i = 0; i < Obj.num_in(); i++) {
            CCU_THROW_IF_FAILED(::asc::ccu_variable_alloc(&formals[i].handle), "call_func: ccu_variable_alloc failed");
            CCU_THROW_IF_FAILED(
                ::asc::ccu_func_define_in_arg(handle, formals[i].handle), "call_func: ccu_func_define_in_arg failed");
        }

        // 执行 lambda（在合成模式下 emit 到 func_block）
        Obj.run_body(formals.empty() ? nullptr : formals.data());

        CCU_THROW_IF_FAILED(::asc::ccu_func_block_end(handle), "call_func: ccu_func_block_end failed");
    }

    // emit 一条 func_call 跳转
    std::vector<::ccu_variable_handle> actuals(arg_num);
    {
        std::size_t i = 0;
        // C++14 兼容的参数包展开
        using expand_t = int[];
        (void)expand_t{0, ((void)(actuals[i++] = args_.handle), 0)...};
        (void)i;
    }
    return ::asc::ccu_func_call(handle, actuals.empty() ? nullptr : actuals.data(), static_cast<uint32_t>(arg_num));
}

} // namespace ccu
} // namespace AscendC

#endif // CCU_FUNC_HPP
