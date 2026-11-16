/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_api/ccu_primitives.h"

#include <vector>

#include "ccu/ccu_api/ccu_primitives_impl.h"
#include "ccu/ccu_api/ccu_utils.h"

namespace asc {
namespace ccu {

// ==================== 资源创建 ====================

template <>
variable get_res_by_channel<variable>(ChannelHandle channel, uint32_t var_index)
{
    variable v{detail::no_alloc_tag{}};
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_create_by_channel(channel, var_index, &v.handle), "ccu_variable_create_by_channel: failed");
    return v;
}

// ==================== 事件 ====================

ccu_result event_record(event e, uint16_t mask) { return ::asc::ccu_event_record(e.handle, mask); }

ccu_result event_wait(event e, uint16_t mask) { return ::asc::ccu_event_wait(e.handle, mask); }

ccu_result event_record(const char* notify_tag, uint16_t mask)
{
    return ::asc::ccu_local_notify_record(notify_tag, mask);
}

ccu_result event_wait(const char* notify_tag, uint16_t mask) { return ::asc::ccu_local_notify_wait(notify_tag, mask); }

ccu_result notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask)
{
    return ::asc::ccu_notify_record(channel, remote_notify_idx, mask);
}

ccu_result notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask)
{
    return ::asc::ccu_notify_wait(channel, local_notify_idx, mask);
}

ccu_result write_variable_with_notify(
    ChannelHandle channel, variable var, uint32_t remote_var_idx, uint32_t remote_notify_idx, uint16_t mask)
{
    return ::asc::ccu_write_variable_with_notify(channel, var.handle, remote_var_idx, remote_notify_idx, mask);
}

// ==================== 加载 ====================

ccu_result load_arg(variable v, uint32_t arg_id) { return ::asc::ccu_load_arg(v.handle, arg_id); }

ccu_result load(uint64_t addr, array<variable>& v_arr, uint32_t num)
{
    return ::asc::ccu_load_var(addr, v_arr[0].handle, num);
}

ccu_result load(uint64_t addr, variable v) { return ::asc::ccu_load_var(addr, v.handle, 1); }

ccu_result load(variable addr_var, array<variable>& v_arr, uint32_t num)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, v_arr[0].handle, num);
}

ccu_result load(variable addr_var, variable v)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, v.handle, 1);
}

ccu_result store(uint64_t addr, array<variable>& v_arr, uint32_t num)
{
    return ::asc::ccu_store_var(addr, v_arr[0].handle, num);
}

ccu_result store(uint64_t addr, variable v) { return ::asc::ccu_store_var(addr, v.handle, 1); }

ccu_result store(variable addr_var, array<variable>& v_arr, uint32_t num)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, v_arr[0].handle, num);
}

ccu_result store(variable addr_var, variable v)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, v.handle, 1);
}

// ==================== 本地拷贝（3 种重载） ====================

ccu_result local_copy(local_addr dst, local_addr src, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_local_copy_mem_to_mem(dst.handle, src.handle, len.handle, evt.handle, mask);
}

ccu_result local_copy(ccu_buffer dst, local_addr src, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_local_copy_mem_to_buffer(dst.handle, src.handle, len.handle, evt.handle, mask);
}

ccu_result local_copy(local_addr dst, ccu_buffer src, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_local_copy_buffer_to_mem(dst.handle, src.handle, len.handle, evt.handle, mask);
}

// ==================== 本地 Reduce ====================

ccu_result local_reduce(
    local_addr dst, local_addr src, variable len, HcclDataType data_type, HcclReduceOp op_type, event evt,
    uint16_t mask)
{
    return ::asc::ccu_local_mem_reduce(dst.handle, src.handle, len.handle, data_type, op_type, evt.handle, mask);
}

ccu_result local_reduce(
    ccu_buffer* buffers, uint32_t count, HcclDataType data_type, HcclDataType output_data_type, HcclReduceOp op_type,
    variable len, event evt, uint16_t mask)
{
    if (buffers == nullptr || count == 0) {
        return ccu_result::CCU_E_PARA;
    }
    std::vector<ccu_buffer_handle> buf_handles(count);
    for (uint32_t i = 0; i < count; i++) {
        buf_handles[i] = buffers[i].handle;
    }
    return ::asc::ccu_local_buffer_reduce(
        buf_handles.data(), count, data_type, output_data_type, op_type, len.handle, evt.handle, mask);
}

// ==================== 远端读====================

ccu_result read(ChannelHandle ch, local_addr local, remote_addr remote, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_read_mem_to_mem(ch, local.handle, remote.handle, len.handle, evt.handle, mask);
}

ccu_result read(ChannelHandle ch, ccu_buffer local, remote_addr remote, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_read_mem_to_buffer(ch, local.handle, remote.handle, len.handle, evt.handle, mask);
}

ccu_result read_reduce(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len, HcclDataType data_type, HcclReduceOp op_type,
    event evt, uint16_t mask)
{
    return ::asc::ccu_read_mem_to_mem_reduce(
        ch, local.handle, remote.handle, len.handle, data_type, op_type, evt.handle, mask);
}

// ==================== 远端写 ====================

ccu_result write(ChannelHandle ch, remote_addr remote, local_addr local, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_write_mem_to_mem(ch, remote.handle, local.handle, len.handle, evt.handle, mask);
}

ccu_result write(ChannelHandle ch, remote_addr remote, ccu_buffer local, variable len, event evt, uint16_t mask)
{
    return ::asc::ccu_write_buffer_to_mem(ch, remote.handle, local.handle, len.handle, evt.handle, mask);
}

ccu_result write_reduce(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len, HcclDataType data_type, HcclReduceOp op_type,
    event evt, uint16_t mask)
{
    return ::asc::ccu_write_mem_to_mem_reduce(
        ch, remote.handle, local.handle, len.handle, data_type, op_type, evt.handle, mask);
}

} // namespace ccu
} // namespace asc
