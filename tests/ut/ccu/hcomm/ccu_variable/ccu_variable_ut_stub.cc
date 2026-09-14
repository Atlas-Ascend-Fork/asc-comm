/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// ccu::variable 运算符 / 控制流宏 UT 专用录制桩：
// - 完整定义 ccu_primitives_impl.cc 引用到的全部 ccu_kernel 方法（真实 ccu_kernel.cc 不参与链接），
//   每个方法仅记录调用名与实参并返回 CCU_SUCCESS；
// - 定义 ccu_kernel_mgr::get_instance / get_current_kernel，把 current kernel 指向
//   asc::ut::set_current_kernel 设置的录制桩对象；
// - Alloc 类方法向出参写入递增伪句柄，供上层断言句柄传递正确性。
#include "ccu_variable_ut_stub.h"

#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

namespace asc {
namespace ut {
namespace {

struct stub_state {
    ccu_kernel* current = nullptr;
    std::vector<kernel_call> log;
    CcuResult pending_fail = CCU_SUCCESS;
    bool fail_armed = false;
    uint64_t handle_seq = 0x1000;
};

stub_state& st()
{
    static stub_state instance;
    return instance;
}

void record(const char* name, std::initializer_list<uint64_t> args, const char* label = nullptr)
{
    kernel_call call;
    call.name = name;
    std::size_t idx = 0;
    for (uint64_t arg : args) {
        if (idx >= (sizeof(call.args) / sizeof(call.args[0]))) {
            break;
        }
        call.args[idx++] = arg;
    }
    call.label = label;
    st().log.push_back(std::move(call));
}

CcuResult take_result()
{
    if (st().fail_armed) {
        st().fail_armed = false;
        return st().pending_fail;
    }
    return CCU_SUCCESS;
}

uint64_t alloc_handle() { return st().handle_seq++; }

} // namespace

void set_current_kernel(ccu_kernel* kernel) { st().current = kernel; }

ccu_kernel* current_kernel() { return st().current; }

void clear_calls() { st().log.clear(); }

const std::vector<kernel_call>& calls() { return st().log; }

std::size_t count_of(const char* name)
{
    std::size_t count = 0;
    for (const kernel_call& call : st().log) {
        if (call.name == name) {
            ++count;
        }
    }
    return count;
}

const kernel_call* last_call(const char* name)
{
    const kernel_call* found = nullptr;
    for (const kernel_call& call : st().log) {
        if (call.name == name) {
            found = &call;
        }
    }
    return found;
}

const kernel_call* first_call(const char* name)
{
    for (const kernel_call& call : st().log) {
        if (call.name == name) {
            return &call;
        }
    }
    return nullptr;
}

void fail_next(CcuResult code)
{
    st().pending_fail = code;
    st().fail_armed = true;
}

ccu_kernel* recording_kernel()
{
    static ccu_kernel instance;
    return &instance;
}

} // namespace ut

// ========== ccu_kernel_mgr 桩：current kernel 由测试控制 ==========
ccu_kernel_mgr& ccu_kernel_mgr::get_instance(int32_t device_logic_id)
{
    (void)device_logic_id;
    // 泄漏式单例：避免引入 ~ccu_kernel_mgr 符号依赖
    static ccu_kernel_mgr* instance = new ccu_kernel_mgr();
    return *instance;
}

ccu_kernel* ccu_kernel_mgr::get_current_kernel() { return ut::current_kernel(); }

// ========== ccu_kernel 录制桩：ccu_primitives_impl.cc 引用的全部方法 ==========
// ---- Alloc 相关接口 ----
CcuResult ccu_kernel::variable_alloc(ccu_variable_handle* var_handle)
{
    ut::record("variable_alloc", {});
    if (var_handle != nullptr) {
        *var_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::address_alloc(ccu_address_handle* addr_handle)
{
    ut::record("address_alloc", {});
    if (addr_handle != nullptr) {
        *addr_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::event_alloc(ccu_event_handle* event_handle)
{
    ut::record("event_alloc", {});
    if (event_handle != nullptr) {
        *event_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::buffer_alloc(ccu_buffer_handle* buf_handle)
{
    ut::record("buffer_alloc", {});
    if (buf_handle != nullptr) {
        *buf_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::local_addr_alloc(
    ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    ut::record("local_addr_alloc", {});
    if (local_addr_handle != nullptr) {
        *local_addr_handle = ut::alloc_handle();
    }
    if (addr_handle != nullptr) {
        *addr_handle = ut::alloc_handle();
    }
    if (token_handle != nullptr) {
        *token_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::remote_addr_alloc(
    ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    ut::record("remote_addr_alloc", {});
    if (remote_addr_handle != nullptr) {
        *remote_addr_handle = ut::alloc_handle();
    }
    if (addr_handle != nullptr) {
        *addr_handle = ut::alloc_handle();
    }
    if (token_handle != nullptr) {
        *token_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count)
{
    ut::record("block_variable_alloc", {count});
    for (uint32_t i = 0; i < count; ++i) {
        var_handles[i] = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::block_event_alloc(ccu_event_handle* event_handles, uint32_t count)
{
    ut::record("block_event_alloc", {count});
    for (uint32_t i = 0; i < count; ++i) {
        event_handles[i] = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count)
{
    ut::record("block_buffer_alloc", {count});
    for (uint32_t i = 0; i < count; ++i) {
        buf_handles[i] = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::variable_create_by_channel(
    ChannelHandle channel, uint32_t var_index, ccu_variable_handle* var_handle)
{
    ut::record("variable_create_by_channel", {static_cast<uint64_t>(channel), var_index});
    if (var_handle != nullptr) {
        *var_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::variable_create_by_acquire(
    ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle)
{
    ut::record("variable_create_by_acquire", {acq_handle, index});
    if (var_handle != nullptr) {
        *var_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::event_create_by_acquire(
    ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle)
{
    ut::record("event_create_by_acquire", {acq_handle, index});
    if (event_handle != nullptr) {
        *event_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

// ---- 运算重载 相关接口 ----
CcuResult ccu_kernel::variable_assign_imm(ccu_variable_handle var, uint64_t immediate)
{
    ut::record("variable_assign_imm", {var, immediate});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_assign_var(ccu_variable_handle var, ccu_variable_handle var_a)
{
    ut::record("variable_assign_var", {var, var_a});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_add_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_add_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_sub_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_sub_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_mul_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_mul_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_add_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ut::record("variable_add_imm_to_var", {var_handle, var_a_handle, immediate});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_sub_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ut::record("variable_sub_imm_to_var", {var_handle, var_a_handle, immediate});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_mul_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ut::record("variable_mul_imm_to_var", {var_handle, var_a_handle, immediate});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_and_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_and_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_or_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_or_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_xor_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_xor_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_not_var(ccu_variable_handle var_handle, ccu_variable_handle var_a_handle)
{
    ut::record("variable_not_var", {var_handle, var_a_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_shl_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_shl_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::variable_shr_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ut::record("variable_shr_var_to_var", {var_handle, var_a_handle, var_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::address_assign_imm(ccu_address_handle addr, uint64_t immediate)
{
    ut::record("address_assign_imm", {addr, immediate});
    return ut::take_result();
}

CcuResult ccu_kernel::address_assign_var(ccu_address_handle addr_handle, ccu_variable_handle var_handle)
{
    ut::record("address_assign_var", {addr_handle, var_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle)
{
    ut::record("address_assign_addr", {dst_addr_handle, src_addr_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::address_add_var_to_addr(
    ccu_address_handle res_addr, ccu_address_handle lhs_addr, ccu_variable_handle rhs_var)
{
    ut::record("address_add_var_to_addr", {res_addr, lhs_addr, rhs_var});
    return ut::take_result();
}

CcuResult ccu_kernel::address_add_addr_to_addr(
    ccu_address_handle res_addr_handle, ccu_address_handle addr_a_handle, ccu_address_handle addr_b_handle)
{
    ut::record("address_add_addr_to_addr", {res_addr_handle, addr_a_handle, addr_b_handle});
    return ut::take_result();
}

CcuResult ccu_kernel::address_add_assign_var(ccu_address_handle addr, ccu_variable_handle var)
{
    ut::record("address_add_assign_var", {addr, var});
    return ut::take_result();
}

CcuResult ccu_kernel::address_add_imm_to_addr(ccu_address_handle res_addr, ccu_address_handle addr_a, uint16_t imm)
{
    ut::record("address_add_imm_to_addr", {res_addr, addr_a, imm});
    return ut::take_result();
}

// ---- 参数加载类 相关接口 ----
CcuResult ccu_kernel::load_arg(ccu_variable_handle var_handle, uint32_t arg_id)
{
    ut::record("load_arg", {var_handle, arg_id});
    return ut::take_result();
}

CcuResult ccu_kernel::load_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    ut::record("load_var", {addr, var_handle, num});
    return ut::take_result();
}

CcuResult ccu_kernel::ccu_load_var_from_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    ut::record("ccu_load_var_from_var_addr", {addr_handle, var_handle, num});
    return ut::take_result();
}

CcuResult ccu_kernel::store_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    ut::record("store_var", {addr, var_handle, num});
    return ut::take_result();
}

CcuResult ccu_kernel::ccu_store_var_to_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    ut::record("ccu_store_var_to_var_addr", {addr_handle, var_handle, num});
    return ut::take_result();
}

// ---- event 信号同步类 相关接口 ----
CcuResult ccu_kernel::event_record(ccu_event_handle event_handle, uint32_t mask)
{
    ut::record("event_record", {event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::event_wait(ccu_event_handle event_handle, uint32_t mask)
{
    ut::record("event_wait", {event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::notify_record(const ChannelHandle channel, uint32_t remote_notify_idx, uint32_t mask)
{
    ut::record("notify_record", {static_cast<uint64_t>(channel), remote_notify_idx, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::notify_wait(const ChannelHandle channel, uint32_t local_notify_idx, uint32_t mask)
{
    ut::record("notify_wait", {static_cast<uint64_t>(channel), local_notify_idx, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::write_variable_with_notify(
    const ChannelHandle channel, ccu_variable_handle var_handle, uint32_t remote_var_idx, uint32_t remote_notify_idx,
    uint32_t mask)
{
    ut::record(
        "write_variable_with_notify",
        {static_cast<uint64_t>(channel), var_handle, remote_var_idx, remote_notify_idx, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_notify_record(const char* notify_tag, const uint32_t mask)
{
    ut::record("local_notify_record", {mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_notify_wait(const char* notify_tag, const uint32_t mask)
{
    ut::record("local_notify_wait", {mask});
    return ut::take_result();
}

// ---- 本地数据拷贝 / reduce 相关接口 ----
CcuResult ccu_kernel::local_copy_mem_to_buffer(
    ccu_buffer_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    ut::record("local_copy_mem_to_buffer", {dst_handle, src_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_copy_buffer_to_mem(
    ccu_local_addr_handle dst_handle, ccu_buffer_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    ut::record("local_copy_buffer_to_mem", {dst_handle, src_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_copy_mem_to_mem(
    ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    ut::record("local_copy_mem_to_mem", {dst_handle, src_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_mem_reduce(
    ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "local_mem_reduce", {dst_handle, src_handle, len_handle, static_cast<uint64_t>(data_type),
                             static_cast<uint64_t>(op_type), event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::local_buffer_reduce(
    ccu_buffer_handle* buf_handles, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "local_buffer_reduce", {count, static_cast<uint64_t>(data_type), static_cast<uint64_t>(output_data_type),
                                static_cast<uint64_t>(op_type), len_handle, event_handle, mask});
    return ut::take_result();
}

// ---- 远端数据传输操作 ----
CcuResult ccu_kernel::read_mem_to_mem(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "read_mem_to_mem",
        {static_cast<uint64_t>(channel), local_handle, remote_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::read_mem_to_buffer(
    ChannelHandle channel, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "read_mem_to_buffer",
        {static_cast<uint64_t>(channel), local_handle, remote_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::read_mem_to_mem_reduce(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
    uint32_t mask)
{
    ut::record(
        "read_mem_to_mem_reduce",
        {static_cast<uint64_t>(channel), local_handle, remote_handle, len_handle, static_cast<uint64_t>(data_type),
         static_cast<uint64_t>(op_type), event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::write_mem_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "write_mem_to_mem",
        {static_cast<uint64_t>(channel), remote_handle, local_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::write_buffer_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_buffer_handle local_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    ut::record(
        "write_buffer_to_mem",
        {static_cast<uint64_t>(channel), remote_handle, local_handle, len_handle, event_handle, mask});
    return ut::take_result();
}

CcuResult ccu_kernel::write_mem_to_mem_reduce(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
    uint32_t mask)
{
    ut::record(
        "write_mem_to_mem_reduce",
        {static_cast<uint64_t>(channel), remote_handle, local_handle, len_handle, static_cast<uint64_t>(data_type),
         static_cast<uint64_t>(op_type), event_handle, mask});
    return ut::take_result();
}

// ---- 控制流操作 ----
CcuResult ccu_kernel::if_begin(
    ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    ut::record("if_begin", {var, immediate, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::if_begin_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    ut::record("if_begin_var", {lhs_handle, rhs_handle, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::if_else(const char* label)
{
    ut::record("if_else", {}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::if_end(const char* label)
{
    ut::record("if_end", {}, label);
    return ut::take_result();
}

void ccu_kernel::if_label_stack_push(const char* label) { ut::record("if_label_stack_push", {}, label); }

void ccu_kernel::if_label_stack_mark_body_done() { ut::record("if_label_stack_mark_body_done", {}); }

const char* ccu_kernel::if_label_stack_pop_for_else()
{
    ut::record("if_label_stack_pop_for_else", {});
    return nullptr;
}

void ccu_kernel::do_while_label_stack_push(const char* label) { ut::record("do_while_label_stack_push", {}, label); }

const char* ccu_kernel::do_while_label_stack_pop_for_while()
{
    ut::record("do_while_label_stack_pop_for_while", {});
    return nullptr;
}

void ccu_kernel::flush_closable_pending_ifs() { ut::record("flush_closable_pending_ifs", {}); }

CcuResult ccu_kernel::while_begin(
    ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    ut::record("while_begin", {var, immediate, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::while_begin_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    ut::record("while_begin_var", {lhs_handle, rhs_handle, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::while_end(const char* label)
{
    ut::record("while_end", {}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::do_while_begin(const char* label)
{
    ut::record("do_while_begin", {}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::do_while_end(
    ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    ut::record("do_while_end", {var, immediate, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

CcuResult ccu_kernel::do_while_end_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    ut::record("do_while_end_var", {lhs_handle, rhs_handle, static_cast<uint64_t>(cond_type)}, label);
    return ut::take_result();
}

// ---- 循环操作 ----
CcuResult ccu_kernel::loop_create(ccu_loop* loop)
{
    ut::record("loop_create", {});
    if (loop != nullptr) {
        *loop = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::loop_body_enter(ccu_loop loop)
{
    ut::record("loop_body_enter", {loop});
    return ut::take_result();
}

CcuResult ccu_kernel::loop_body_exit(ccu_loop loop)
{
    ut::record("loop_body_exit", {loop});
    return ut::take_result();
}

CcuResult ccu_kernel::loop_group_create(
    ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config)
{
    ut::record("loop_group_create", {max_loop_num});
    if (group != nullptr) {
        *group = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::loop_group_create_from_var(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var_handle,
    ccu_variable_handle offset_var_handle)
{
    ut::record("loop_group_create_from_var", {max_loop_num, parallel_var_handle, offset_var_handle});
    if (group != nullptr) {
        *group = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config)
{
    ut::record("loop_group_add_loop", {group, loop});
    return ut::take_result();
}

CcuResult ccu_kernel::loop_group_add_loop_from_var(
    ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var)
{
    ut::record("loop_group_add_loop_from_var", {group, loop, loop_param_var});
    return ut::take_result();
}

// ---- 函数调用操作 ----
CcuResult ccu_kernel::func_block_lookup(const void* func_ptr, uint64_t* out_handle)
{
    ut::record("func_block_lookup", {reinterpret_cast<uint64_t>(func_ptr)});
    if (out_handle != nullptr) {
        *out_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::func_block_begin(const void* func_ptr, uint64_t* out_handle)
{
    ut::record("func_block_begin", {reinterpret_cast<uint64_t>(func_ptr)});
    if (out_handle != nullptr) {
        *out_handle = ut::alloc_handle();
    }
    return ut::take_result();
}

CcuResult ccu_kernel::func_block_end(uint64_t handle)
{
    ut::record("func_block_end", {handle});
    return ut::take_result();
}

CcuResult ccu_kernel::func_define_in_arg(uint64_t handle, ccu_variable_handle formal)
{
    ut::record("func_define_in_arg", {handle, formal});
    return ut::take_result();
}

CcuResult ccu_kernel::func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in)
{
    ut::record("func_call", {handle, num_in});
    return ut::take_result();
}

} // namespace asc
