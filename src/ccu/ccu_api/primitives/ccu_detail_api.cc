/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_api/ccu_array.h"
#include "ccu/ccu_api/ccu_event.h"
#include "ccu/ccu_api/ccu_buffer.h"

#include "ccu/ccu_api/ccu_func.h"
#include "ccu/ccu_api/ccu_control_flow_macro.h"
#include "ccu/ccu_api/ccu_print.h"

#include "ccu/ccu_api/ccu_primitives_impl.h"

namespace asc {
namespace ccu {

// ==================== array traits ====================

ccu_result ccu_array_traits<variable>::block_alloc(ccu_variable_handle* h, uint32_t n)
{
    return ::asc::ccu_block_variable_alloc(h, n);
}

ccu_result ccu_array_traits<variable>::create_by_acquire(
    ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* h)
{
    return ::asc::ccu_variable_get_by_index(acq_handle, index, h);
}

ccu_result ccu_array_traits<event>::block_alloc(ccu_event_handle* h, uint32_t n)
{
    return ::asc::ccu_block_event_alloc(h, n);
}

ccu_result ccu_array_traits<event>::create_by_acquire(ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* h)
{
    return ::asc::ccu_event_get_by_index(acq_handle, index, h);
}

ccu_result ccu_array_traits<ccu_buffer>::block_alloc(ccu_buffer_handle* h, uint32_t n)
{
    return ::asc::ccu_block_buffer_alloc(h, n);
}

// ==================== func ====================

namespace detail {

ccu_result asc_ccu_func_block_lookup(const void* key, uint64_t* handle)
{
    return ::asc::ccu_func_block_lookup(key, handle);
}

ccu_result asc_ccu_func_block_begin(const void* key, uint64_t* handle)
{
    return ::asc::ccu_func_block_begin(key, handle);
}

ccu_result asc_ccu_func_block_end(uint64_t handle) { return ::asc::ccu_func_block_end(handle); }

ccu_result asc_ccu_func_define_in_arg(uint64_t handle, ccu_variable_handle var)
{
    return ::asc::ccu_func_define_in_arg(handle, var);
}

ccu_result asc_ccu_variable_alloc_handle(ccu_variable_handle* handle) { return ::asc::ccu_variable_alloc(handle); }

ccu_result asc_ccu_func_call(uint64_t handle, const ccu_variable_handle* actuals, uint32_t arg_num)
{
    return ::asc::ccu_func_call(handle, const_cast<ccu_variable_handle*>(actuals), arg_num);
}

} // namespace detail

// ==================== print ====================

namespace dfx {
namespace detail {

ccu_result ccu_if_begin(ccu_variable_handle lhs, uint64_t rhs, ccu_condition_type condition, const char* label)
{
    return ::asc::ccu_if_begin(lhs, rhs, condition, label);
}

ccu_result ccu_if_end(const char* label) { return ::asc::ccu_if_end(label); }

} // namespace detail
} // namespace dfx

} // namespace ccu
} // namespace asc

namespace asc {
namespace ccu {

namespace detail {

ccu_result ccu_if_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_if_begin(var, immediate, cond_type, label);
}

ccu_result ccu_if_begin_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_if_begin_var(lhs, rhs, cond_type, label);
}

ccu_result ccu_if_else(const char* label) { return ::asc::ccu_if_else(label); }

ccu_result ccu_if_end(const char* label) { return ::asc::ccu_if_end(label); }

ccu_result ccu_flush_pending_ifs() { return ::asc::ccu_flush_pending_ifs(); }

ccu_result ccu_while_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_while_begin(var, immediate, cond_type, label);
}

ccu_result ccu_while_begin_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_while_begin_var(lhs, rhs, cond_type, label);
}

ccu_result ccu_while_end(const char* label) { return ::asc::ccu_while_end(label); }

ccu_result ccu_do_while_begin(const char* label) { return ::asc::ccu_do_while_begin(label); }

ccu_result ccu_do_while_end(
    ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_do_while_end(var, immediate, cond_type, label);
}

ccu_result ccu_do_while_end_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    return ::asc::ccu_do_while_end_var(lhs, rhs, cond_type, label);
}

void ccu_if_stack_push(const char* label) { ::asc::ccu_if_stack_push(label); }

void ccu_if_stack_mark_body_done() { ::asc::ccu_if_stack_mark_body_done(); }

const char* ccu_if_stack_pop_for_else() { return ::asc::ccu_if_stack_pop_for_else(); }

void ccu_do_while_stack_push(const char* label) { ::asc::ccu_do_while_stack_push(label); }

const char* ccu_do_while_stack_pop_for_while() { return ::asc::ccu_do_while_stack_pop_for_while(); }

} // namespace detail

} // namespace ccu
} // namespace asc
