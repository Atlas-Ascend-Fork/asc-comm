/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// 数据面 primitive C ABI 实现入口。
// 头部统一走 hcomm 包内 header（asccomm_ccu_channel.h / hcomm_types.h），
// 不再 include hccl 私有头，从源码层面落实控制面/数据面边界。
#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/hcomm_types.h"
#include "ccu/hcomm/ccu_primitives_impl.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_device_context.h"

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

namespace asc {

// Alloc 相关接口
CcuResult ccu_variable_alloc(ccu_variable_handle* var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_alloc(var_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_alloc(ccu_address_handle* addr_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_alloc(addr_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_event_alloc(ccu_event_handle* event_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->event_alloc(event_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_buffer_alloc(ccu_buffer_handle* buf_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->buffer_alloc(buf_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_local_addr_alloc(
    ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_addr_alloc(local_addr_handle, addr_handle, token_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_remote_addr_alloc(
    ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->remote_addr_alloc(remote_addr_handle, addr_handle, token_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->block_variable_alloc(var_handles, count));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_block_event_alloc(ccu_event_handle* event_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->block_event_alloc(event_handles, count));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->block_buffer_alloc(buf_handles, count));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_variable_create_by_channel(ChannelHandle channel, uint32_t var_index, ccu_variable_handle* var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_create_by_channel(channel, var_index, var_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_variable_get_by_index(ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle)
{
    CCU_CHK_PTR_NULL(var_handle);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_create_by_acquire(acq_handle, index, var_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_event_get_by_index(ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle)
{
    CCU_CHK_PTR_NULL(event_handle);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->event_create_by_acquire(acq_handle, index, event_handle));
    return CcuResult::CCU_SUCCESS;
}

// Variable操作类 相关接口
CcuResult ccu_variable_assign_imm(ccu_variable_handle res_var, uint64_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_assign_imm(res_var, immediate));

    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_variable_assign_var(ccu_variable_handle dst_var_handle, ccu_variable_handle src_var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_assign_var(dst_var_handle, src_var_handle));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_variable_add_var_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_add_var_to_var(res_var, var_a, var_b));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_variable_shl_var_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_shl_var_to_var(res_var, var_a, var_b));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_variable_shr_var_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->variable_shr_var_to_var(res_var, var_a, var_b));

    return CcuResult::CCU_SUCCESS;
}

/*
Address 相关接口
*/
CcuResult ccu_address_assign_imm(ccu_address_handle addr, uint64_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_assign_imm(addr, immediate));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_assign_addr(dst_addr_handle, src_addr_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_assign_var(ccu_address_handle addr, ccu_variable_handle var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_assign_var(addr, var));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_add_var_to_addr(
    ccu_address_handle res_addr, ccu_address_handle lhs_addr, ccu_variable_handle rhs_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_add_var_to_addr(res_addr, lhs_addr, rhs_var));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_add_addr_to_addr(
    ccu_address_handle res_addr, ccu_address_handle addr_a, ccu_address_handle addr_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_add_addr_to_addr(res_addr, addr_a, addr_b));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_address_add_assign_var(ccu_address_handle addr, ccu_variable_handle var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->address_add_assign_var(addr, var));
    return CcuResult::CCU_SUCCESS;
}

// 参数加载类 相关接口
CcuResult ccu_load_arg(ccu_variable_handle var_handle, uint32_t arg_id)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->load_arg(var_handle, arg_id));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_load_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[CcuLoadVar] invalid args, num[%u]", num);
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->load_var(addr, var_handle, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_load_var_from_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[CcuLoadVarFromVarAddr] invalid args, num[%u]", num);
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->ccu_load_var_from_var_addr(addr_handle, var_handle, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_store_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[CcuStoreVar] invalid args, num[%u]", num);
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->store_var(addr, var_handle, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_store_var_to_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[CcuStoreVarToVarAddr] invalid args, num[%u]", num);
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->ccu_store_var_to_var_addr(addr_handle, var_handle, num));
    return CcuResult::CCU_SUCCESS;
}

// Event信号同步类 相关接口
CcuResult ccu_event_record(ccu_event_handle event_handle, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->event_record(event_handle, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_event_wait(ccu_event_handle event_handle, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->event_wait(event_handle, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->notify_record(channel, remote_notify_idx, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->notify_wait(channel, local_notify_idx, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_write_variable_with_notify(
    ChannelHandle channel, ccu_variable_handle var_handle, uint32_t remote_var_idx, uint32_t remote_notify_idx,
    uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->write_variable_with_notify(channel, var_handle, remote_var_idx, remote_notify_idx, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_local_notify_record(const char* notify_tag, uint16_t mask)
{
    CCU_CHK_PTR_NULL(notify_tag);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_notify_record(notify_tag, mask));
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_local_notify_wait(const char* notify_tag, uint16_t mask)
{
    CCU_CHK_PTR_NULL(notify_tag);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_notify_wait(notify_tag, mask));
    return CcuResult::CCU_SUCCESS;
}

// 本地数据拷贝 相关接口
CcuResult ccu_local_copy_mem_to_mem(
    ccu_local_addr_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, ccu_event_handle event,
    uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_copy_mem_to_mem(dst, src, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_local_copy_mem_to_buffer(
    ccu_buffer_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_copy_mem_to_buffer(dst, src, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_local_copy_buffer_to_mem(
    ccu_local_addr_handle dst, ccu_buffer_handle src, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_copy_buffer_to_mem(dst, src, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}
// 本地reduce 相关接口
CcuResult ccu_local_mem_reduce(
    ccu_local_addr_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, HcclDataType data_type,
    HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_mem_reduce(dst, src, len, data_type, op_type, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_local_buffer_reduce(
    ccu_buffer_handle* buffers, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    if (buffers == nullptr || count == 0) {
        HCCL_ERROR("[CcuLocalBufferReduce] invalid args, buffers[%p] count[%u]", buffers, count);
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->local_buffer_reduce(buffers, count, data_type, output_data_type, op_type, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

/* ========== 远端数据传输操作 ========== */
CcuResult ccu_read_mem_to_mem(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->read_mem_to_mem(channel, local_handle, remote_handle, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_read_mem_to_buffer(
    ChannelHandle channel, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->read_mem_to_buffer(channel, local_handle, remote_handle, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_read_mem_to_mem_reduce(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(
        kernel->read_mem_to_mem_reduce(channel, local_handle, remote_handle, len, data_type, op_type, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_write_mem_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_local_addr_handle local, ccu_variable_handle len,
    ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->write_mem_to_mem(channel, remote, local, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_write_buffer_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_buffer_handle local, ccu_variable_handle len,
    ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->write_buffer_to_mem(channel, remote, local, len, event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_write_mem_to_mem_reduce(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_local_addr_handle local, ccu_variable_handle len,
    HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->write_mem_to_mem_reduce(channel, remote, local, len, data_type, op_type, event, mask));
    return CcuResult::CCU_SUCCESS;
}

/* ========== 控制流操作 ========== */
CcuResult ccu_if_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->if_begin(var, immediate, cond_type, label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_if_else(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->if_else(label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_if_end(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->if_end(label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_flush_pending_ifs()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    kernel->flush_closable_pending_ifs();
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_while_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->while_begin(var, immediate, cond_type, label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_while_end(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->while_end(label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_do_while_begin(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->do_while_begin(label));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_do_while_end(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->do_while_end(var, immediate, cond_type, label));

    return CcuResult::CCU_SUCCESS;
}

/* ========== 函数调用操作 ========== */
CcuResult ccu_func_block_lookup(const void* func_ptr, uint64_t* out_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->func_block_lookup(func_ptr, out_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_func_block_begin(const void* func_ptr, uint64_t* out_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->func_block_begin(func_ptr, out_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_func_block_end(uint64_t handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->func_block_end(handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_func_define_in_arg(uint64_t handle, ccu_variable_handle formal)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->func_define_in_arg(handle, formal));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->func_call(handle, in_args, num_in));
    return CcuResult::CCU_SUCCESS;
}

/* ========== 循环操作 ========== */
CcuResult ccu_loop_create(ccu_loop* loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_create(loop));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_body_enter(ccu_loop loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_body_enter(loop));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_body_exit(ccu_loop loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_body_exit(loop));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_group_create(ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_group_create(group, max_loop_num, config));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_group_create_from_var(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var, ccu_variable_handle offset_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_group_create_from_var(group, max_loop_num, parallel_var, offset_var));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_group_add_loop(group, loop, config));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_loop_group_add_loop_from_var(ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL(kernel);
    CCU_CHK_RET(kernel->loop_group_add_loop_from_var(group, loop, loop_param_var));
    return CcuResult::CCU_SUCCESS;
}

// 控制流标签栈 C 接口（_CcuIfStack* / _CcuDoWhileStack*）

void ccu_if_stack_push(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[_CcuIfStackPush] no current kernel, label=%s", label != nullptr ? label : "(null)");
        return;
    }
    kernel->if_label_stack_push(label);
}

void ccu_if_stack_mark_body_done()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[_CcuIfStackMarkBodyDone] no current kernel");
        return;
    }
    kernel->if_label_stack_mark_body_done();
}

const char* ccu_if_stack_pop_for_else()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[_CcuIfStackPopForElse] no current kernel");
        return nullptr;
    }
    return kernel->if_label_stack_pop_for_else();
}

void ccu_do_while_stack_push(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[_CcuDoWhileStackPush] no current kernel, label=%s", label != nullptr ? label : "(null)");
        return;
    }
    kernel->do_while_label_stack_push(label);
}

const char* ccu_do_while_stack_pop_for_while()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        // 见上方注释：CCU_WHILE 每次都会调本函数做模式判别，保持沉默。
        return nullptr;
    }
    return kernel->do_while_label_stack_pop_for_while();
}

} // namespace asc
