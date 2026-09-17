/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_PRIMITIVES_HPP
#define CCU_PRIMITIVES_HPP

#include <vector>

#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_control_flow_macro.h"

#include "ccu/hcomm/ccu_variable.hpp"
#include "ccu/hcomm/ccu_address.hpp"
#include "ccu/hcomm/ccu_event.hpp"
#include "ccu/hcomm/ccu_buffer.hpp"
#include "ccu/hcomm/ccu_local_addr.hpp"
#include "ccu/hcomm/ccu_remote_addr.hpp"
#include "ccu/hcomm/ccu_array.hpp"
#include "ccu/hcomm/ccu_func.hpp"
#include "ccu/hcomm/ccu_loop.hpp"

namespace AscendC {
namespace ccu {

// ==================== 类型别名 ====================
using loop_config = ::ccu_loop_config;
using loop_group_config = ::ccu_loop_group_config;

// ==================== 资源创建 ====================

template <typename t>
inline t GetResByChannel(ChannelHandle /* channel */, uint32_t /* index */)
{
    static_assert(
        sizeof(t) == 0, "ccu::GetResByChannel<t> is not specialized for this type t; "
                        "currently supported: variable.");
}
template <>
inline variable GetResByChannel<variable>(ChannelHandle channel, uint32_t var_index)
{
    variable v{detail::no_alloc_tag{}};
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_create_by_channel(channel, var_index, &v.handle),
        "ccu_variable_create_by_channel: failed");
    return v;
}

// ==================== 事件 ====================
inline CcuResult event_record(event e, uint16_t mask = 1) { return ::asc::ccu_event_record(e.handle, mask); }
inline CcuResult event_wait(event e, uint16_t mask = 1) { return ::asc::ccu_event_wait(e.handle, mask); }
inline CcuResult event_record(const char* notify_tag, uint16_t mask = 1)
{
    return ::asc::ccu_local_notify_record(notify_tag, mask);
}
inline CcuResult event_wait(const char* notify_tag, uint16_t mask = 1)
{
    return ::asc::ccu_local_notify_wait(notify_tag, mask);
}
inline CcuResult notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask = 1)
{
    return ::asc::ccu_notify_record(channel, remote_notify_idx, mask);
}
inline CcuResult notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask = 1)
{
    return ::asc::ccu_notify_wait(channel, local_notify_idx, mask);
}
inline CcuResult write_variable_with_notify(
    ChannelHandle channel, variable var, uint32_t remote_var_idx, uint32_t remote_notify_idx, uint16_t mask = 1)
{
    return ::asc::ccu_write_variable_with_notify(channel, var.handle, remote_var_idx, remote_notify_idx, mask);
}

// ==================== 加载 ====================
inline CcuResult load_arg(variable v, uint32_t arg_id) { return ::asc::ccu_load_arg(v.handle, arg_id); }
inline CcuResult load(uint64_t addr, array<variable>& var_arr, uint32_t num)
{
    return ::asc::ccu_load_var(addr, var_arr[0].handle, num);
}
inline CcuResult load(uint64_t addr, variable v) { return ::asc::ccu_load_var(addr, v.handle, 1); }
inline CcuResult load(variable addr_var, array<variable>& var_arr, uint32_t num)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, var_arr[0].handle, num);
}
inline CcuResult load(variable addr_var, variable v)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, v.handle, 1);
}
inline CcuResult store(uint64_t addr, array<variable>& var_arr, uint32_t num)
{
    return ::asc::ccu_store_var(addr, var_arr[0].handle, num);
}
inline CcuResult store(uint64_t addr, variable v) { return ::asc::ccu_store_var(addr, v.handle, 1); }
inline CcuResult store(variable addr_var, array<variable>& var_arr, uint32_t num)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, var_arr[0].handle, num);
}
inline CcuResult store(variable addr_var, variable v)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, v.handle, 1);
}

// ==================== 本地拷贝（3 种重载） ====================
// local_addr → local_addr,local_addr → ccu_buffer,ccu_buffer → local_addr
inline CcuResult local_copy(local_addr dst, local_addr src, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_local_copy_mem_to_mem(dst.handle, src.handle, len.handle, event.handle, mask);
}
inline CcuResult local_copy(ccu_buffer dst, local_addr src, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_local_copy_mem_to_buffer(dst.handle, src.handle, len.handle, event.handle, mask);
}
inline CcuResult local_copy(local_addr dst, ccu_buffer src, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_local_copy_buffer_to_mem(dst.handle, src.handle, len.handle, event.handle, mask);
}

// ==================== 本地 Reduce ====================
inline CcuResult LocalReduce(
    local_addr dst, local_addr src, variable len, HcclDataType data_type, HcclReduceOp op_type, event event,
    uint16_t mask = 1)
{
    return ::asc::ccu_local_mem_reduce(
        dst.handle, src.handle, len.handle, data_type, op_type, event.handle, mask);
}
inline CcuResult LocalReduce(
    ccu_buffer* buffers, uint32_t count, HcclDataType data_type, HcclDataType output_data_type, HcclReduceOp op_type,
    variable len, event event, uint16_t mask = 1)
{
    if (buffers == nullptr || count == 0) {
        return CcuResult::CCU_E_PARA;
    }
    std::vector<ccu_buffer_handle> buf_handles(count);
    for (uint32_t i = 0; i < count; i++) {
        buf_handles[i] = buffers[i].handle;
    }
    return ::asc::ccu_local_buffer_reduce(
        buf_handles.data(), count, data_type, output_data_type, op_type, len.handle, event.handle, mask);
}

// ==================== 远端读====================

// 远端读 local_addr ← remote_addr
inline CcuResult Read(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_read_mem_to_mem(ch, local.handle, remote.handle, len.handle, event.handle, mask);
}
// 远端读 ccu_buffer ← remote_addr
inline CcuResult Read(
    ChannelHandle ch, ccu_buffer local, remote_addr remote, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_read_mem_to_buffer(ch, local.handle, remote.handle, len.handle, event.handle, mask);
}
// 远端读 local_addr ← remote_addr Reduce (Reduce)
inline CcuResult ReadReduce(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len, HcclDataType data_type,
    HcclReduceOp op_type, event event, uint16_t mask = 1)
{
    return ::asc::ccu_read_mem_to_mem_reduce(
        ch, local.handle, remote.handle, len.handle, data_type, op_type, event.handle, mask);
}

// ==================== 远端写 ====================

// local_addr → remote_addr
inline CcuResult write(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_write_mem_to_mem(ch, remote.handle, local.handle, len.handle, event.handle, mask);
}
// ccu_buffer → remote_addr
inline CcuResult write(
    ChannelHandle ch, remote_addr remote, ccu_buffer local, variable len, event event, uint16_t mask = 1)
{
    return ::asc::ccu_write_buffer_to_mem(ch, remote.handle, local.handle, len.handle, event.handle, mask);
}
// local_addr → remote_addr Reduce (Reduce)
inline CcuResult write_reduce(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len, HcclDataType data_type,
    HcclReduceOp op_type, event event, uint16_t mask = 1)
{
    return ::asc::ccu_write_mem_to_mem_reduce(
        ch, remote.handle, local.handle, len.handle, data_type, op_type, event.handle, mask);
}

} // namespace ccu
} // namespace AscendC

#endif // CCU_API_HPP
