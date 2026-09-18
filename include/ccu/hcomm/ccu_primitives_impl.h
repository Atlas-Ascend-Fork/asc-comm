/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_PRIMITIVES_IMPL_H
#define CCU_PRIMITIVES_IMPL_H

#ifdef __cplusplus
#include <cstdbool>
#else
#error "ccu/hcomm/ccu_primitives_impl.h 仅支持 C++：数据面 primitive 声明位于 namespace asc"
#endif // __cplusplus

// 控制面/数据面 ABI 边界：数据类型（HcclResult 等）改从 hcomm 包内 hcomm_types.h 获取，
// 不再直接依赖 hccl/hccl_types.h，保证两仓按统一 POD ABI 演进
#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/hcomm_primitives.h"
#include "hcomm/hcomm_types.h"

// 数据面 primitive 统一为 C++ linkage：声明与实现均位于 namespace asc，
// 与 hcomm 仓保留 C ABI 的同名接口（pkg_inc/hcomm/ccu/ccu_primitives_impl.h）符号隔离，
// 避免两个 SO 导出相同未修饰 C 符号、链接顺序决定实现选择。
namespace asc {

// Alloc 相关接口
extern CcuResult ccu_variable_alloc(ccu_variable_handle* var_handle);
extern CcuResult ccu_address_alloc(ccu_address_handle* addr_handle);
extern CcuResult ccu_event_alloc(ccu_event_handle* event_handle);
extern CcuResult ccu_buffer_alloc(ccu_buffer_handle* buf_handle);
extern CcuResult ccu_local_addr_alloc(
    ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle);
extern CcuResult ccu_remote_addr_alloc(
    ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle);

// BlockAlloc 相关接口
extern CcuResult ccu_block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count_);
extern CcuResult ccu_block_event_alloc(ccu_event_handle* event_handles, uint32_t count_);
extern CcuResult ccu_block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count_);

extern CcuResult ccu_variable_create_by_channel(
    ChannelHandle channel_, uint32_t var_index, ccu_variable_handle* var_handle);

extern CcuResult ccu_variable_get_by_index(
    ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle);

extern CcuResult ccu_event_get_by_index(ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle);

// variable操作类 相关接口
extern CcuResult ccu_variable_assign_imm(ccu_variable_handle res_var, uint64_t immediate_);
extern CcuResult ccu_variable_assign_var(ccu_variable_handle dst_var_handle, ccu_variable_handle src_var_handle);
extern CcuResult ccu_variable_add_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b);
extern CcuResult ccu_variable_shl_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b);
extern CcuResult ccu_variable_shr_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b);

// address操作类 相关接口
extern CcuResult ccu_address_assign_imm(ccu_address_handle addr_, uint64_t immediate_);
extern CcuResult ccu_address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle);
extern CcuResult ccu_address_assign_var(ccu_address_handle addr_, ccu_variable_handle var_);
extern CcuResult ccu_address_add_var_to_addr(
    ccu_address_handle res_addr, ccu_address_handle lhs_addr, ccu_variable_handle rhs_var);
extern CcuResult ccu_address_add_addr_to_addr(
    ccu_address_handle res_addr, ccu_address_handle addr_a, ccu_address_handle addr_b);
extern CcuResult ccu_address_add_assign_var(ccu_address_handle addr_, ccu_variable_handle var_);

// 参数加载类 相关接口
extern CcuResult ccu_load_arg(ccu_variable_handle var_handle, uint32_t arg_id);
extern CcuResult ccu_load_var(uint64_t addr_, ccu_variable_handle var_handle, uint32_t num_);
extern CcuResult ccu_load_var_from_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num_);
extern CcuResult ccu_store_var(uint64_t addr_, ccu_variable_handle var_handle, uint32_t num_);
extern CcuResult ccu_store_var_to_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num_);

// event信号同步类 相关接口
//  mask_ 由调用方独立传入（与 event 句柄解耦）；CcuSetMask 已废弃删除。
extern CcuResult ccu_event_record(ccu_event_handle event_handle, uint16_t mask_);
extern CcuResult ccu_event_wait(ccu_event_handle event_handle, uint16_t mask_);
extern CcuResult ccu_notify_record(ChannelHandle channel_, uint32_t remote_notify_idx, uint16_t mask_);
extern CcuResult ccu_notify_wait(ChannelHandle channel_, uint32_t local_notify_idx, uint16_t mask_);
extern CcuResult ccu_write_variable_with_notify(
    ChannelHandle channel_, ccu_variable_handle var_handle, uint32_t remote_var_idx, uint32_t remote_notify_idx,
    uint16_t mask_);
// 本地（同 device 内跨 core）通知同步：与 ccu_notify_record/wait 的区别在于
// 通知对端用 coreId 标识（同卡内某个 core），而不是 ChannelHandle（跨 rank 通道）。
extern CcuResult ccu_local_notify_record(const char* notify_tag, uint16_t mask_);
extern CcuResult ccu_local_notify_wait(const char* notify_tag, uint16_t mask_);

// 本地数据拷贝 相关接口
extern CcuResult ccu_local_copy_mem_to_mem(
    ccu_local_addr_handle dst_, ccu_local_addr_handle src_, ccu_variable_handle len_, ccu_event_handle event,
    uint16_t mask_);
extern CcuResult ccu_local_copy_mem_to_buffer(
    ccu_buffer_handle dst_, ccu_local_addr_handle src_, ccu_variable_handle len_, ccu_event_handle event,
    uint16_t mask_);
extern CcuResult ccu_local_copy_buffer_to_mem(
    ccu_local_addr_handle dst_, ccu_buffer_handle src_, ccu_variable_handle len_, ccu_event_handle event,
    uint16_t mask_);
// 本地reduce 相关接口
extern CcuResult ccu_local_mem_reduce(
    ccu_local_addr_handle dst_, ccu_local_addr_handle src_, ccu_variable_handle len_, HcclDataType data_type,
    HcclReduceOp op_type, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_local_buffer_reduce(
    ccu_buffer_handle* buffers, uint32_t count_, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, ccu_variable_handle len_, ccu_event_handle event, uint16_t mask_);

/* ========== 远端数据传输操作 ========== */
extern CcuResult ccu_read_mem_to_mem(
    ChannelHandle channel_, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_read_mem_to_buffer(
    ChannelHandle channel_, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_read_mem_to_mem_reduce(
    ChannelHandle channel_, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_write_mem_to_mem(
    ChannelHandle channel_, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_write_buffer_to_mem(
    ChannelHandle channel_, ccu_remote_addr_handle remote_handle, ccu_buffer_handle local_handle,
    ccu_variable_handle len_, ccu_event_handle event, uint16_t mask_);
extern CcuResult ccu_write_mem_to_mem_reduce(
    ChannelHandle channel_, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask_);

/* ========== 控制流操作 ========== */
extern CcuResult ccu_if_begin(
    ccu_variable_handle var_, uint64_t immediate_, ccu_condition_type cond_type, const char* label_);
extern CcuResult ccu_if_else(const char* label_);
extern CcuResult ccu_if_end(const char* label_);
extern CcuResult ccu_flush_pending_ifs();
extern CcuResult ccu_while_begin(
    ccu_variable_handle var_, uint64_t immediate_, ccu_condition_type cond_type, const char* label_);
extern CcuResult ccu_while_end(const char* label_);
extern CcuResult ccu_do_while_begin(const char* label_);
extern CcuResult ccu_do_while_end(
    ccu_variable_handle var_, uint64_t immediate_, ccu_condition_type cond_type, const char* label_);

/* ========== 函数调用操作 ========== */
extern CcuResult ccu_func_block_lookup(const void* func_ptr, uint64_t* out_handle);
extern CcuResult ccu_func_block_begin(const void* func_ptr, uint64_t* out_handle);
extern CcuResult ccu_func_block_end(uint64_t handle);
extern CcuResult ccu_func_define_in_arg(uint64_t handle, ccu_variable_handle formal);
extern CcuResult ccu_func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in);

/*
 * 控制流宏内部使用的标签栈接口（以 _ 前缀标识为内部 API，
 * 仅供 ccu_control_flow_macro.h 中的 CCU_IF / CCU_ELSE / CCU_DO / CCU_WHILE
 * 等宏在调用现场展开时使用）。
 */
extern void ccu_if_stack_push(const char* label_);
extern void ccu_if_stack_mark_body_done();
extern const char* ccu_if_stack_pop_for_else();

extern void ccu_do_while_stack_push(const char* label_);
extern const char* ccu_do_while_stack_pop_for_while();

/* ========== 循环操作 ========== */
extern CcuResult ccu_loop_create(ccu_loop* loop);
extern CcuResult ccu_loop_body_enter(ccu_loop loop);
extern CcuResult ccu_loop_body_exit(ccu_loop loop);
// loop_group 创建时按需扩容 LoopEngine 池：传入本 group 实际要 add_loop 的次数
// （含展开复用）。各 loop_group 之间通过 local loop_idx 复用低位 executorId，
// 因此池子按"取最大值"被动扩容，不会按组累加。
extern CcuResult ccu_loop_group_create(
    ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config);
extern CcuResult ccu_loop_group_create_from_var(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var, ccu_variable_handle offset_var);
extern CcuResult ccu_loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config);
extern CcuResult ccu_loop_group_add_loop_from_var(
    ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var);

} // namespace asc

#endif // CCU_PRIMITIVES_IMPL_H
