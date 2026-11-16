/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_api/ccu_func.h"
#include "ccu/ccu_api/ccu_loop.h"
#include "ccu/ccu_api/ccu_variable.h"
#include "ccu/ccu_api/ccu_address.h"
#include "ccu/ccu_api/ccu_buffer.h"
#include "ccu/ccu_api/ccu_event.h"
#include "ccu/ccu_api/ccu_local_addr.h"
#include "ccu/ccu_api/ccu_remote_addr.h"

#include "ccu/ccu_api/ccu_primitives_impl.h"

namespace asc {
namespace ccu {

// ==================== variable ====================

variable::variable() { CCU_THROW_IF_FAILED(::asc::ccu_variable_alloc(&this->handle), "ccu_variable_alloc: failed"); }

variable::variable(ccu_variable_handle var_handle, uint32_t index)
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_get_by_index(var_handle, index, &this->handle), "ccu_variable_get_by_index: failed");
}

void variable::operator=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_assign_var(this->handle, other.handle),
        "variable::operator=(variable): ccu_variable_assign_var failed");
}

void variable::operator=(uint64_t immediate) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_assign_imm(this->handle, immediate),
        "variable::operator=(uint64_t): ccu_variable_assign_imm failed");
}

void variable::operator=(detail::ccu_arithmetic_operator<variable, variable> op) const
{
    switch (op.type_) {
        case detail::ccu_arithmetic_operator_type::addition:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_add_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var+Var): ccu_variable_add_var_to_var failed");
            break;
        case detail::ccu_arithmetic_operator_type::subtraction:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_sub_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var-Var): ccu_variable_sub_var_to_var failed");
            break;
        case detail::ccu_arithmetic_operator_type::multiplication:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_mul_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var*Var): ccu_variable_mul_var_to_var failed");
            break;
        default:
            throw detail::ccu_exception(
                ccu_result::CCU_E_PARA, "variable::operator=: invalid arithmetic operator type_");
    }
}

void variable::operator=(detail::ccu_arithmetic_operator<variable, uint16_t> op) const
{
    switch (op.type_) {
        case detail::ccu_arithmetic_operator_type::addition:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_add_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                "variable::operator=(Var+Imm): ccu_variable_add_imm_to_var failed");
            break;
        case detail::ccu_arithmetic_operator_type::subtraction:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_sub_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                "variable::operator=(Var-Imm): ccu_variable_sub_imm_to_var failed");
            break;
        case detail::ccu_arithmetic_operator_type::multiplication:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_mul_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                "variable::operator=(Var*Imm): ccu_variable_mul_imm_to_var failed");
            break;
        default:
            throw detail::ccu_exception(
                ccu_result::CCU_E_PARA, "variable::operator=: invalid arithmetic operator type_");
    }
}

void variable::operator+=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_add_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator+=(variable): ccu_variable_add_var_to_var failed");
}

void variable::operator-=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_sub_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator-=(variable): ccu_variable_sub_var_to_var failed");
}

void variable::operator*=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_mul_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator*=(variable): ccu_variable_mul_var_to_var failed");
}

void variable::operator+=(uint16_t immediate) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_add_imm_to_var(this->handle, this->handle, immediate),
        "variable::operator+=(uint16_t): ccu_variable_add_imm_to_var failed");
}

void variable::operator-=(uint16_t immediate) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_sub_imm_to_var(this->handle, this->handle, immediate),
        "variable::operator-=(uint16_t): ccu_variable_sub_imm_to_var failed");
}

void variable::operator*=(uint16_t immediate) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_mul_imm_to_var(this->handle, this->handle, immediate),
        "variable::operator*=(uint16_t): ccu_variable_mul_imm_to_var failed");
}

void variable::operator=(detail::ccu_logic_operator<variable, variable> op) const
{
    switch (op.type_) {
        case detail::ccu_logic_operator_type::and_op:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_and_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var&Var): ccu_variable_and_var_to_var failed");
            break;
        case detail::ccu_logic_operator_type::or_op:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_or_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var|Var): ccu_variable_or_var_to_var failed");
            break;
        case detail::ccu_logic_operator_type::xor_op:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_xor_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var^Var): ccu_variable_xor_var_to_var failed");
            break;
        default:
            throw detail::ccu_exception(ccu_result::CCU_E_PARA, "variable::operator=: invalid logic operator type_");
    }
}

void variable::operator=(detail::ccu_logic_unary_operator<variable> op) const
{
    switch (op.type_) {
        case detail::ccu_logic_operator_type::not_op:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_not_var(this->handle, op.lhs.handle),
                "variable::operator=(~Var): ccu_variable_not_var failed");
            break;
        default:
            throw detail::ccu_exception(
                ccu_result::CCU_E_PARA, "variable::operator=: invalid unary logic operator type_");
    }
}

void variable::operator=(detail::ccu_shift_operator<variable, variable> op) const
{
    switch (op.type_) {
        case detail::ccu_shift_operator_type::left:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_shl_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var<<Var): ccu_variable_shl_var_to_var failed");
            break;
        case detail::ccu_shift_operator_type::right:
            CCU_THROW_IF_FAILED(
                ::asc::ccu_variable_shr_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                "variable::operator=(Var>>Var): ccu_variable_shr_var_to_var failed");
            break;
        default:
            throw detail::ccu_exception(ccu_result::CCU_E_PARA, "variable::operator=: invalid shift operator type_");
    }
}

void variable::operator&=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_and_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator&=(variable): ccu_variable_and_var_to_var failed");
}

void variable::operator|=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_or_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator|=(variable): ccu_variable_or_var_to_var failed");
}

void variable::operator^=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_xor_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator^=(variable): ccu_variable_xor_var_to_var failed");
}

void variable::operator<<=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_shl_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator<<=(variable): ccu_variable_shl_var_to_var failed");
}

void variable::operator>>=(const variable& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_shr_var_to_var(this->handle, this->handle, other.handle),
        "variable::operator>>=(variable): ccu_variable_shr_var_to_var failed");
}

// ==================== address ====================

address::address() { CCU_THROW_IF_FAILED(::asc::ccu_address_alloc(&this->handle), "ccu_address_alloc: failed"); }

void address::operator=(const address& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_assign_addr(this->handle, other.handle),
        "address::operator=(address): ccu_address_assign_addr failed");
}

void address::operator=(uint64_t immediate) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_assign_imm(this->handle, immediate),
        "address::operator=(uint64_t): ccu_address_assign_imm failed");
}

void address::operator=(const variable& var) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_assign_var(this->handle, var.handle),
        "address::operator=(variable): ccu_address_assign_var failed");
}

void address::operator=(detail::ccu_arithmetic_operator<address, address> op) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_addr_to_addr(this->handle, op.lhs.handle, op.rhs.handle),
        "address::operator=(Addr+Addr): ccu_address_add_addr_to_addr failed");
}

void address::operator=(detail::ccu_arithmetic_operator<address, variable> op) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_var_to_addr(this->handle, op.lhs.handle, op.rhs.handle),
        "address::operator=(Addr+Var): ccu_address_add_var_to_addr failed");
}

void address::operator=(detail::ccu_arithmetic_operator<variable, address> op) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_var_to_addr(this->handle, op.rhs.handle, op.lhs.handle),
        "address::operator=(Var+Addr): ccu_address_add_var_to_addr failed");
}

void address::operator=(detail::ccu_arithmetic_operator<address, uint16_t> op) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_imm_to_addr(this->handle, op.lhs.handle, op.rhs),
        "address::operator=(Addr+Imm): ccu_address_add_imm_to_addr failed");
}

void address::operator+=(const variable& var) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_assign_var(this->handle, var.handle),
        "address::operator+=(variable): ccu_address_add_assign_var failed");
}

void address::operator+=(const address& other) const
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_address_add_addr_to_addr(this->handle, this->handle, other.handle),
        "address::operator+=(address): ccu_address_add_addr_to_addr failed");
}

// ==================== ccu_buffer ====================

ccu_buffer::ccu_buffer() { CCU_THROW_IF_FAILED(::asc::ccu_buffer_alloc(&this->handle), "ccu_buffer_alloc: failed"); }

// ==================== event ====================

event::event() { CCU_THROW_IF_FAILED(::asc::ccu_event_alloc(&this->handle), "ccu_event_alloc: failed"); }

event::event(ccu_event_handle acq_handle, uint32_t index)
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_event_get_by_index(acq_handle, index, &this->handle), "ccu_event_get_by_index: failed");
}

// ==================== local_addr ====================

local_addr::local_addr() : addr(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_local_addr_alloc(&this->handle, &this->addr.handle, &this->token.handle),
        "ccu_local_addr_alloc: failed");
}

// ==================== remote_addr ====================

remote_addr::remote_addr() : addr(detail::no_alloc_tag{}), token(detail::no_alloc_tag{})
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_remote_addr_alloc(&this->handle, &this->addr.handle, &this->token.handle),
        "ccu_remote_addr_alloc: failed");
}

// ==================== loop ====================

loop::loop(variable& loop_cfg, const func& func)
{
    compose_loop_body(func);
    is_var_based_ = true;
    loop_param_var_ = &loop_cfg;
}

loop::loop(const ccu_loop_config& loop_cfg, const func& func)
{
    compose_loop_body(func);
    is_var_based_ = false;
    config_ = loop_cfg;
}

void loop::compose_loop_body(const func& func)
{
    if (func.num_in() != 0) {
        throw ::asc::ccu::detail::ccu_exception(ccu_result::CCU_E_PARA, "ccu::loop requires a no-argument ccu::func");
    }
    CCU_THROW_IF_FAILED(::asc::ccu_loop_create(&handle_), "ccu_loop_create failed");
    CCU_THROW_IF_FAILED(::asc::ccu_loop_body_enter(handle_), "ccu_loop_body_enter failed");
    try {
        func.run_body(nullptr);
    } catch (...) {
        (void)::asc::ccu_loop_body_exit(handle_);
        throw;
    }
    CCU_THROW_IF_FAILED(::asc::ccu_loop_body_exit(handle_), "ccu_loop_body_exit failed");
}

// ==================== loop_group ====================

loop_group::loop_group(
    variable& parallel_cfg, variable& offset_cfg, uint32_t max_loop_num, const std::vector<loop>& loops)
{
    CCU_THROW_IF_FAILED(
        ::asc::ccu_loop_group_create_from_var(&handle_, max_loop_num, parallel_cfg.handle, offset_cfg.handle),
        "ccu_loop_group_create_from_var failed");
    add_loops(loops);
}

loop_group::loop_group(
    const ccu_loop_group_config& loop_group_cfg, uint32_t max_loop_num, const std::vector<loop>& loops)
{
    // 旧 config 与版本化 cfg 都归一化为 ccu_loop_group_cfg，统一走 cfg 路径；
    // 版本头由封装统一盖，用户无需关心。
    ccu_loop_group_cfg local_cfg = to_cfg(loop_group_cfg);
    CCU_THROW_IF_FAILED(
        ::asc::ccu_loop_group_create_cfg(&handle_, max_loop_num, &local_cfg), "ccu_loop_group_create_cfg failed");
    add_loops(loops);
}

ccu_loop_cfg loop_group::to_cfg(const ccu_loop_config& config)
{
    ccu_loop_cfg cfg{};
    ccu_loop_cfg_init(&cfg);
    cfg.addr_offset = config.addr_offset;
    cfg.iter_num = config.iter_num;
    return cfg;
}

ccu_loop_group_cfg loop_group::to_cfg(const ccu_loop_group_config& config)
{
    ccu_loop_group_cfg cfg{};
    ccu_loop_group_cfg_init(&cfg);
    cfg.clone_num = config.clone_num;
    cfg.clone_loop_offset = config.clone_loop_offset;
    cfg.addr_offset = config.addr_offset;
    cfg.ccu_buffer_offset = config.ccu_buffer_offset;
    cfg.event_offset = config.event_offset;
    cfg.var_offset = 0; // 旧 config 无 var_offset
    return cfg;
}

void loop_group::add_loops(const std::vector<loop>& loops)
{
    for (const auto& l : loops) {
        if (l.is_var_based()) {
            auto* loop_param_var = l.loop_param_var();
            if (loop_param_var == nullptr) {
                throw ::asc::ccu::detail::ccu_exception(
                    ccu_result::CCU_E_PARA, "ccu::loop var-based loop has null loop parameter");
            }
            CCU_THROW_IF_FAILED(
                ::asc::ccu_loop_group_add_loop_from_var(handle_, l.handle(), loop_param_var->handle),
                "ccu_loop_group_add_loop_from_var failed");
        } else {
            ccu_loop_cfg local_cfg = to_cfg(*l.config());
            CCU_THROW_IF_FAILED(
                ::asc::ccu_loop_group_add_loop_cfg(handle_, l.handle(), &local_cfg),
                "ccu_loop_group_add_loop_cfg failed");
        }
    }
}

} // namespace ccu
} // namespace asc
