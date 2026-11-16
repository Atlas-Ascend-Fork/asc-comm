/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_PRIMITIVES_H
#define CCU_PRIMITIVES_H

#include "ccu/ccu_api/ccu_control_flow_macro.h"
#include "ccu_api/ccu/hccl_types.h"

#include "ccu/ccu_api/ccu_variable.h"
#include "ccu/ccu_api/ccu_address.h"
#include "ccu/ccu_api/ccu_event.h"
#include "ccu/ccu_api/ccu_buffer.h"
#include "ccu/ccu_api/ccu_local_addr.h"
#include "ccu/ccu_api/ccu_remote_addr.h"
#include "ccu/ccu_api/ccu_array.h"
#include "ccu/ccu_api/ccu_func.h"
#include "ccu/ccu_api/ccu_loop.h"

namespace asc {
namespace ccu {

// ==================== 类型别名 ====================
using loop_config = ::ccu_loop_config;
using loop_group_config = ::ccu_loop_group_config;

// ==================== 资源创建 ====================

template <typename t>
inline t get_res_by_channel(ChannelHandle /* channel */, uint32_t /* index */)
{
    static_assert(
        sizeof(t) == 0, "ccu::get_res_by_channel<t> is not specialized for this type t; "
                        "currently supported: variable.");
}

template <>
variable get_res_by_channel<variable>(ChannelHandle channel, uint32_t var_index);

// ==================== 事件 ====================
ccu_result event_record(event e, uint16_t mask = 1);
ccu_result event_wait(event e, uint16_t mask = 1);
ccu_result event_record(const char* notify_tag, uint16_t mask = 1);
ccu_result event_wait(const char* notify_tag, uint16_t mask = 1);
ccu_result notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask = 1);
ccu_result notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask = 1);
ccu_result write_variable_with_notify(
    ChannelHandle channel, variable var, uint32_t remote_var_idx, uint32_t remote_notify_idx, uint16_t mask = 1);

// ==================== 加载 ====================
ccu_result load_arg(variable v, uint32_t arg_id);
ccu_result load(uint64_t addr, array<variable>& v_arr, uint32_t num);
ccu_result load(uint64_t addr, variable v);
ccu_result load(variable addr_var, array<variable>& v_arr, uint32_t num);
ccu_result load(variable addr_var, variable v);
ccu_result store(uint64_t addr, array<variable>& v_arr, uint32_t num);
ccu_result store(uint64_t addr, variable v);
ccu_result store(variable addr_var, array<variable>& v_arr, uint32_t num);
ccu_result store(variable addr_var, variable v);

// ==================== 本地拷贝（3 种重载） ====================
// local_addr → local_addr,local_addr → ccu_buffer,ccu_buffer → local_addr
ccu_result local_copy(local_addr dst, local_addr src, variable len, event evt, uint16_t mask = 1);
ccu_result local_copy(ccu_buffer dst, local_addr src, variable len, event evt, uint16_t mask = 1);
ccu_result local_copy(local_addr dst, ccu_buffer src, variable len, event evt, uint16_t mask = 1);

// ==================== 本地 Reduce ====================
ccu_result local_reduce(
    local_addr dst, local_addr src, variable len, HcclDataType data_type, HcclReduceOp op_type, event evt,
    uint16_t mask = 1);
ccu_result local_reduce(
    ccu_buffer* buffers, uint32_t count, HcclDataType data_type, HcclDataType output_data_type, HcclReduceOp op_type,
    variable len, event evt, uint16_t mask = 1);

// ==================== 远端读====================

// 远端读 local_addr ← remote_addr
ccu_result read(ChannelHandle ch, local_addr local, remote_addr remote, variable len, event evt, uint16_t mask = 1);
// 远端读 ccu_buffer ← remote_addr
ccu_result read(ChannelHandle ch, ccu_buffer local, remote_addr remote, variable len, event evt, uint16_t mask = 1);
// 远端读 local_addr ← remote_addr Reduce
ccu_result read_reduce(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len, HcclDataType data_type, HcclReduceOp op_type,
    event evt, uint16_t mask = 1);

// ==================== 远端写 ====================

// local_addr → remote_addr
ccu_result write(ChannelHandle ch, remote_addr remote, local_addr local, variable len, event evt, uint16_t mask = 1);
// ccu_buffer → remote_addr
ccu_result write(ChannelHandle ch, remote_addr remote, ccu_buffer local, variable len, event evt, uint16_t mask = 1);
// local_addr → remote_addr Reduce
ccu_result write_reduce(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len, HcclDataType data_type, HcclReduceOp op_type,
    event evt, uint16_t mask = 1);

} // namespace ccu
} // namespace asc

#endif // CCU_PRIMITIVES_H
