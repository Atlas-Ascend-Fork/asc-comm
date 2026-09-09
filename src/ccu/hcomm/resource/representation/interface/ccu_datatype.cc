/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

uint16_t ccu_phy_res::id() const { return id_; }
uint16_t ccu_phy_res::die_id() const { return die_id_; }
void ccu_phy_res::reset(uint16_t id) { this->id_ = id; }
void ccu_phy_res::set_die_id(uint16_t die_id) { this->die_id_ = die_id; }

ccu_vir_res::ccu_vir_res(ccu_rep_context* context) : context_(context) { phy_res_ = std::make_shared<ccu_phy_res>(); }

void ccu_vir_res::reset(uint16_t id) { phy_res_->reset(id); }

void ccu_vir_res::reset(uint16_t id, uint16_t die_id)
{
    phy_res_->reset(id);
    phy_res_->set_die_id(die_id);
}

void ccu_vir_res::set_die_id(uint16_t die_id) { phy_res_->set_die_id(die_id); }

uint16_t ccu_vir_res::id() const { return phy_res_->id(); }

uint16_t ccu_vir_res::die_id() const { return phy_res_->die_id(); }

variable::variable(ccu_rep_context* context) : ccu_vir_res(context) {}

variable::variable(const variable& other) : ccu_vir_res(other.context_) { phy_res_ = other.phy_res_; }

void variable::operator=(variable&& other)
{
    phy_res_ = other.phy_res_;
    context_ = other.context_;
}

void variable::operator=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_assign>(context_->get_ins_generator(), *this, other));
}

void variable::operator=(uint64_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_assign>(context_->get_ins_generator(), *this, immediate));
}

template <typename dst_t, typename lhs, typename rhs>
void arithmetic_append_to_context(ccu_rep_context* context, dst_t& dst, ccu_arithmetic_operator<lhs, rhs> op)
{
    switch (op.type) {
        case ccu_arithmetic_operator_type::addition:
            append_to_context(
                context, std::make_shared<ccu_rep_add>(context->get_ins_generator(), dst, op.lhs, op.rhs));
            break;
        case ccu_arithmetic_operator_type::multiplication:
            append_to_context(
                context, std::make_shared<ccu_rep_mul>(context->get_ins_generator(), dst, op.lhs, op.rhs));
            break;
        case ccu_arithmetic_operator_type::subtraction:
            append_to_context(
                context, std::make_shared<ccu_rep_sub>(context->get_ins_generator(), dst, op.lhs, op.rhs));
            break;
        default:
            asc::throw_ccu_not_support("Not supported Arithmetic operator[%d].", op.type);
    }
}

void variable::var_var_append_to_context(ccu_arithmetic_operator<variable, variable> op)
{
    arithmetic_append_to_context(context_, *this, op);
}

void variable::operator=(ccu_arithmetic_operator<variable, variable> op) { var_var_append_to_context(op); }

void variable::var_immed_append_to_context(ccu_arithmetic_operator<variable, uint16_t> op)
{
    arithmetic_append_to_context(context_, *this, op);
}

void variable::operator=(ccu_arithmetic_operator<variable, uint16_t> op) { var_immed_append_to_context(op); }

void variable::addr_immed_append_to_context(ccu_arithmetic_operator<address, uint16_t> op)
{
    arithmetic_append_to_context(context_, *this, op);
}

void variable::operator=(ccu_arithmetic_operator<address, uint16_t> op) { addr_immed_append_to_context(op); }

void variable::addr_addr_append_to_context(ccu_arithmetic_operator<address, address> op)
{
    switch (op.type) {
        case ccu_arithmetic_operator_type::addition: {
            append_to_context(
                context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Arithmetic operate in variable = addr (op) addr.");
        }
    }
}

void variable::operator=(ccu_arithmetic_operator<address, address> op) { addr_addr_append_to_context(op); }

void variable::operator+=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, other));
}

void variable::operator+=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, immediate));
}

void variable::operator*=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, other));
}

void variable::operator*=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, immediate));
}

void variable::operator-=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_sub>(context_->get_ins_generator(), *this, other));
}

void variable::operator-=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_sub>(context_->get_ins_generator(), *this, immediate));
}

void variable::operator&=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_and>(context_->get_ins_generator(), *this, other));
}

void variable::operator|=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_or>(context_->get_ins_generator(), *this, other));
}

void variable::operator^=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_xor>(context_->get_ins_generator(), *this, other));
}

void variable::var_var_logic_append_to_context(ccu_logic_operator<variable, variable> op)
{
    switch (op.type) {
        case ccu_logic_operator_type::and_op: {
            append_to_context(
                context_, std::make_shared<ccu_rep_and>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_logic_operator_type::or_op: {
            append_to_context(
                context_, std::make_shared<ccu_rep_or>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_logic_operator_type::xor_op: {
            append_to_context(
                context_, std::make_shared<ccu_rep_xor>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Logic operate between var and var");
        }
    }
}

void variable::addr_logic_append_to_context(ccu_logic_operator<variable> op)
{
    switch (op.type) {
        case ccu_logic_operator_type::not_op: {
            append_to_context(context_, std::make_shared<ccu_rep_not>(context_->get_ins_generator(), *this, op.lhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Logic operate between addr and immedB.");
        }
    }
}

void variable::operator=(ccu_logic_operator<variable, variable> op) { var_var_logic_append_to_context(op); }

void variable::operator=(ccu_logic_operator<variable> op) { addr_logic_append_to_context(op); }

void variable::operator=(ccu_shift_operator<variable, variable> op)
{
    switch (op.type) {
        case ccu_shift_type::right: {
            append_to_context(
                context_, std::make_shared<ccu_rep_sh_r>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_shift_type::left: {
            append_to_context(
                context_, std::make_shared<ccu_rep_sh_l>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported shift bit operate between var and immed.");
        }
    }
}

void variable::operator<<=(const variable& other) const
{
    append_to_context(context_, std::make_shared<ccu_rep_sh_l>(context_->get_ins_generator(), *this, other));
}

void variable::operator>>=(const variable& other) const
{
    append_to_context(context_, std::make_shared<ccu_rep_sh_r>(context_->get_ins_generator(), *this, other));
}

address::address(ccu_rep_context* context) : ccu_vir_res(context) {}

address::address(const address& other) : ccu_vir_res(other.context_) { phy_res_ = other.phy_res_; }

address::address(variable&& other) : ccu_vir_res(other.get_cur_context()) { phy_res_ = other.get_cur_phy_res(); }

void address::operator=(address&& other)
{
    phy_res_ = other.phy_res_;
    context_ = other.context_;
}

void address::operator=(const address& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_assign>(context_->get_ins_generator(), *this, other));
}

void address::operator=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_assign>(context_->get_ins_generator(), *this, other));
}

void address::operator=(uint64_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_assign>(context_->get_ins_generator(), *this, immediate));
}

void address::var_addr_append_to_context(ccu_arithmetic_operator<variable, address> op)
{
    switch (op.type) {
        case ccu_arithmetic_operator_type::addition: {
            append_to_context(
                context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, op.rhs, op.lhs));
            break;
        }
        case ccu_arithmetic_operator_type::multiplication: {
            append_to_context(
                context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_arithmetic_operator_type::subtraction: {
            append_to_context(
                context_, std::make_shared<ccu_rep_sub>(context_->get_ins_generator(), *this, op.rhs, op.lhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Arithmetic operate between var and addr.");
        }
    }
}
void address::operator=(ccu_arithmetic_operator<variable, address> op) { var_addr_append_to_context(op); }

void address::addr_addr_append_to_context(ccu_arithmetic_operator<address, address> op)
{
    switch (op.type) {
        case ccu_arithmetic_operator_type::addition: {
            append_to_context(
                context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Arithmetic operate in addr = addr (op) addr .");
        }
    }
}

void address::operator=(ccu_arithmetic_operator<address, address> op) { addr_addr_append_to_context(op); }

void address::var_immed_append_to_context(ccu_arithmetic_operator<variable, uint16_t> op)
{
    arithmetic_append_to_context(context_, *this, op);
}
void address::operator=(ccu_arithmetic_operator<variable, uint16_t> op) { var_immed_append_to_context(op); }

void address::addr_immed_append_to_context(ccu_arithmetic_operator<address, uint16_t> op)
{
    arithmetic_append_to_context(context_, *this, op);
}
void address::operator=(ccu_arithmetic_operator<address, uint16_t> op) { addr_immed_append_to_context(op); }

void address::var_var_append_to_context(ccu_arithmetic_operator<variable, variable> op)
{
    switch (op.type) {
        case ccu_arithmetic_operator_type::addition: {
            append_to_context(
                context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_arithmetic_operator_type::multiplication: {
            append_to_context(
                context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported Arithmetic operate between var and var.");
        }
    }
}

void address::operator=(ccu_arithmetic_operator<variable, variable> op) { var_var_append_to_context(op); }

void address::operator+=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, other));
}

void address::operator+=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_add>(context_->get_ins_generator(), *this, immediate));
}

void address::operator*=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, other));
}

void address::operator*=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_mul>(context_->get_ins_generator(), *this, immediate));
}

void address::operator-=(const variable& other)
{
    append_to_context(context_, std::make_shared<ccu_rep_sub>(context_->get_ins_generator(), *this, other));
}

void address::operator-=(const uint16_t immediate)
{
    append_to_context(context_, std::make_shared<ccu_rep_sub>(context_->get_ins_generator(), *this, immediate));
}

void address::operator=(ccu_shift_operator<variable, variable> op)
{
    switch (op.type) {
        case ccu_shift_type::left: {
            append_to_context(
                context_, std::make_shared<ccu_rep_sh_l>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        case ccu_shift_type::right: {
            append_to_context(
                context_, std::make_shared<ccu_rep_sh_r>(context_->get_ins_generator(), *this, op.lhs, op.rhs));
            break;
        }
        default: {
            asc::throw_ccu_not_support("Not supported shift bit operate between var and var.");
        }
    }
}

void address::operator<<=(const variable& other) const
{
    append_to_context(context_, std::make_shared<ccu_rep_sh_l>(context_->get_ins_generator(), *this, other));
}

void address::operator>>=(const variable& other) const
{
    append_to_context(context_, std::make_shared<ccu_rep_sh_r>(context_->get_ins_generator(), *this, other));
}

local_notify::local_notify(ccu_rep_context* context) : ccu_vir_res(context) {}

ccu_buffer::ccu_buffer(ccu_rep_context* context) : ccu_vir_res(context) {}

ccu_buf::ccu_buf(ccu_rep_context* context) : ccu_vir_res(context) {}
uint16_t ccu_buffer::id() const { return phy_res_->id() + ccubuffer_die_id_bit * phy_res_->die_id(); }

uint16_t ccu_buf::id() const { return phy_res_->id() + ccubuffer_die_id_bit * phy_res_->die_id(); }

executor::executor(ccu_rep_context* context) : ccu_vir_res(context) {}

completed_event::completed_event(ccu_rep_context* context) : ccu_vir_res(context) {}

}; // namespace ccu_rep
}; // namespace asc
