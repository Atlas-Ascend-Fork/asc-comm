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
inline t GetResByChannel(ChannelHandle /* channel_ */, uint32_t /* index */)
{
    static_assert(
        sizeof(t) == 0, "ccu::GetResByChannel<t> is not specialized for this type_ t; "
                        "currently supported: variable.");
}
template <>
inline variable GetResByChannel<variable>(ChannelHandle channel_, uint32_t var_index)
{
    variable v{detail::no_alloc_tag{}};
    CCU_THROW_IF_FAILED(
        ::asc::ccu_variable_create_by_channel(channel_, var_index, &v.handle),
        "ccu_variable_create_by_channel: failed");
    return v;
}

// ==================== 事件 ====================
inline CcuResult event_record(event e, uint16_t mask_ = 1) { return ::asc::ccu_event_record(e.handle, mask_); }
inline CcuResult event_wait(event e, uint16_t mask_ = 1) { return ::asc::ccu_event_wait(e.handle, mask_); }
inline CcuResult event_record(const char* notify_tag, uint16_t mask_ = 1)
{
    return ::asc::ccu_local_notify_record(notify_tag, mask_);
}
inline CcuResult event_wait(const char* notify_tag, uint16_t mask_ = 1)
{
    return ::asc::ccu_local_notify_wait(notify_tag, mask_);
}
inline CcuResult notify_record(ChannelHandle channel_, uint32_t remote_notify_idx, uint16_t mask_ = 1)
{
    return ::asc::ccu_notify_record(channel_, remote_notify_idx, mask_);
}
inline CcuResult notify_wait(ChannelHandle channel_, uint32_t local_notify_idx, uint16_t mask_ = 1)
{
    return ::asc::ccu_notify_wait(channel_, local_notify_idx, mask_);
}
inline CcuResult write_variable_with_notify(
    ChannelHandle channel_, variable var_, uint32_t remote_var_idx, uint32_t remote_notify_idx, uint16_t mask_ = 1)
{
    return ::asc::ccu_write_variable_with_notify(channel_, var_.handle, remote_var_idx, remote_notify_idx, mask_);
}

// ==================== 加载 ====================
inline CcuResult load_arg(variable v, uint32_t arg_id_) { return ::asc::ccu_load_arg(v.handle, arg_id_); }
inline CcuResult load(uint64_t addr_, array<variable>& vArr, uint32_t num_)
{
    return ::asc::ccu_load_var(addr_, vArr[0].handle, num_);
}
inline CcuResult load(uint64_t addr_, variable v) { return ::asc::ccu_load_var(addr_, v.handle, 1); }
inline CcuResult load(variable addr_var, array<variable>& vArr, uint32_t num_)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, vArr[0].handle, num_);
}
inline CcuResult load(variable addr_var, variable v)
{
    return ::asc::ccu_load_var_from_var_addr(addr_var.handle, v.handle, 1);
}
inline CcuResult Store(uint64_t addr_, array<variable>& vArr, uint32_t num_)
{
    return ::asc::ccu_store_var(addr_, vArr[0].handle, num_);
}
inline CcuResult Store(uint64_t addr_, variable v) { return ::asc::ccu_store_var(addr_, v.handle, 1); }
inline CcuResult Store(variable addr_var, array<variable>& vArr, uint32_t num_)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, vArr[0].handle, num_);
}
inline CcuResult Store(variable addr_var, variable v)
{
    return ::asc::ccu_store_var_to_var_addr(addr_var.handle, v.handle, 1);
}

// ==================== 本地拷贝（3 种重载） ====================
// local_addr → local_addr,local_addr → ccu_buffer,ccu_buffer → local_addr
inline CcuResult LocalCopy(local_addr dst_, local_addr src_, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_local_copy_mem_to_mem(dst_.handle, src_.handle, len_.handle, event.handle, mask_);
}
inline CcuResult LocalCopy(ccu_buffer dst_, local_addr src_, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_local_copy_mem_to_buffer(dst_.handle, src_.handle, len_.handle, event.handle, mask_);
}
inline CcuResult LocalCopy(local_addr dst_, ccu_buffer src_, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_local_copy_buffer_to_mem(dst_.handle, src_.handle, len_.handle, event.handle, mask_);
}

// ==================== 本地 Reduce ====================
inline CcuResult LocalReduce(
    local_addr dst_, local_addr src_, variable len_, HcclDataType data_type_, HcclReduceOp op_type_, event event,
    uint16_t mask_ = 1)
{
    return ::asc::ccu_local_mem_reduce(
        dst_.handle, src_.handle, len_.handle, data_type_, op_type_, event.handle, mask_);
}
inline CcuResult LocalReduce(
    ccu_buffer* buffers, uint32_t count_, HcclDataType data_type_, HcclDataType outputDataType, HcclReduceOp op_type_,
    variable len_, event event, uint16_t mask_ = 1)
{
    if (buffers == nullptr || count_ == 0) {
        return CcuResult::CCU_E_PARA;
    }
    std::vector<ccu_buffer_handle> buf_handles(count_);
    for (uint32_t i = 0; i < count_; i++) {
        buf_handles[i] = buffers[i].handle;
    }
    return ::asc::ccu_local_buffer_reduce(
        buf_handles.data(), count_, data_type_, outputDataType, op_type_, len_.handle, event.handle, mask_);
}

// ==================== 远端读====================

// 远端读 local_addr ← remote_addr
inline CcuResult Read(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_read_mem_to_mem(ch, local.handle, remote.handle, len_.handle, event.handle, mask_);
}
// 远端读 ccu_buffer ← remote_addr
inline CcuResult Read(
    ChannelHandle ch, ccu_buffer local, remote_addr remote, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_read_mem_to_buffer(ch, local.handle, remote.handle, len_.handle, event.handle, mask_);
}
// 远端读 local_addr ← remote_addr Reduce (Reduce)
inline CcuResult ReadReduce(
    ChannelHandle ch, local_addr local, remote_addr remote, variable len_, HcclDataType data_type_,
    HcclReduceOp op_type_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_read_mem_to_mem_reduce(
        ch, local.handle, remote.handle, len_.handle, data_type_, op_type_, event.handle, mask_);
}

// ==================== 远端写 ====================

// local_addr → remote_addr
inline CcuResult Write(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_write_mem_to_mem(ch, remote.handle, local.handle, len_.handle, event.handle, mask_);
}
// ccu_buffer → remote_addr
inline CcuResult Write(
    ChannelHandle ch, remote_addr remote, ccu_buffer local, variable len_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_write_buffer_to_mem(ch, remote.handle, local.handle, len_.handle, event.handle, mask_);
}
// local_addr → remote_addr Reduce (Reduce)
inline CcuResult WriteReduce(
    ChannelHandle ch, remote_addr remote, local_addr local, variable len_, HcclDataType data_type_,
    HcclReduceOp op_type_, event event, uint16_t mask_ = 1)
{
    return ::asc::ccu_write_mem_to_mem_reduce(
        ch, remote.handle, local.handle, len_.handle, data_type_, op_type_, event.handle, mask_);
}

} // namespace ccu
} // namespace AscendC

#endif // CCU_API_HPP
