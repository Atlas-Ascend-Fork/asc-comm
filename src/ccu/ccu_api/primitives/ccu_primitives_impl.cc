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
#include "hcomm/ccu/hcomm_res_ccu_entity_defs.h"
#include "hccl/hccl_types.h"
// 数据面实现与 hcomm 的 C ABI 契约同名符号，内部保留 ccu_result；对外统一为 ccu_result。
#include "ccu/ccu_api/ccu_primitives_impl.h"

#include "ccu_api/common/ccu_log.h"
#include "ccu_api/common/ccu_device_context.h"

#include "ccu_api/resource/kernel/ccu_kernel_mgr.h"

namespace asc {

// 校验带版本头的 cfg POD：magic / version(1..max) / size 三元组均须匹配。
static ccu_result validate_ccu_cfg_header(const ccu_cfg_header* header, uint32_t expect_size, uint32_t max_version)
{
    if (header == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    if (header->magic != CCU_CFG_MAGIC_WORD) {
        HCCL_ERROR("[validate_ccu_cfg_header] bad magic 0x%x", header->magic);
        return ccu_result::CCU_E_PARA;
    }
    if (header->version == 0 || header->version > max_version) {
        HCCL_ERROR("[validate_ccu_cfg_header] bad version %u (max %u)", header->version, max_version);
        return ccu_result::CCU_E_PARA;
    }
    if (header->size != expect_size) {
        HCCL_ERROR("[validate_ccu_cfg_header] bad size %u (expect %u)", header->size, expect_size);
        return ccu_result::CCU_E_PARA;
    }
    return ccu_result::CCU_SUCCESS;
}

// Alloc 相关接口
ccu_result ccu_variable_alloc(ccu_variable_handle* var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_alloc(var_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_alloc(ccu_address_handle* addr_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_alloc(addr_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_event_alloc(ccu_event_handle* event_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->event_alloc(event_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_buffer_alloc(ccu_buffer_handle* buf_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->buffer_alloc(buf_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_local_addr_alloc(
    ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_addr_alloc(local_addr_handle, addr_handle, token_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_remote_addr_alloc(
    ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->remote_addr_alloc(remote_addr_handle, addr_handle, token_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->block_variable_alloc(var_handles, count));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_block_event_alloc(ccu_event_handle* event_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->block_event_alloc(event_handles, count));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->block_buffer_alloc(buf_handles, count));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_variable_create_by_channel(ChannelHandle channel, uint32_t var_index, ccu_variable_handle* var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_create_by_channel(channel, var_index, var_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_get_by_index(ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle)
{
    CCU_CHK_PTR_NULL_ASC(var_handle);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_create_by_acquire(acq_handle, index, var_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_event_get_by_index(ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle)
{
    CCU_CHK_PTR_NULL_ASC(event_handle);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->event_create_by_acquire(acq_handle, index, event_handle));
    return ccu_result::CCU_SUCCESS;
}

// Variable操作类 相关接口
ccu_result ccu_variable_assign_imm(ccu_variable_handle res_var, uint64_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_assign_imm(res_var, immediate));

    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_variable_assign_var(ccu_variable_handle dst_var_handle, ccu_variable_handle src_var_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_assign_var(dst_var_handle, src_var_handle));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_add_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_add_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_shl_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_shl_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_shr_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_shr_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_sub_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_sub_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_mul_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_mul_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_add_imm_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, uint16_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_add_imm_to_var(res_var, var_a, immediate));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_sub_imm_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, uint16_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_sub_imm_to_var(res_var, var_a, immediate));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_mul_imm_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, uint16_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_mul_imm_to_var(res_var, var_a, immediate));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_and_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_and_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_or_var_to_var(ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_or_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_xor_var_to_var(
    ccu_variable_handle res_var, ccu_variable_handle var_a, ccu_variable_handle var_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_xor_var_to_var(res_var, var_a, var_b));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_variable_not_var(ccu_variable_handle res_var, ccu_variable_handle var_a)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->variable_not_var(res_var, var_a));

    return ccu_result::CCU_SUCCESS;
}

/*
Address 相关接口
*/
ccu_result ccu_address_assign_imm(ccu_address_handle addr, uint64_t immediate)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_assign_imm(addr, immediate));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_assign_addr(dst_addr_handle, src_addr_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_assign_var(ccu_address_handle addr, ccu_variable_handle var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_assign_var(addr, var));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_add_var_to_addr(
    ccu_address_handle res_addr, ccu_address_handle lhs_addr, ccu_variable_handle rhs_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_add_var_to_addr(res_addr, lhs_addr, rhs_var));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_add_addr_to_addr(
    ccu_address_handle res_addr, ccu_address_handle addr_a, ccu_address_handle addr_b)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_add_addr_to_addr(res_addr, addr_a, addr_b));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_add_assign_var(ccu_address_handle addr, ccu_variable_handle var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_add_assign_var(addr, var));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_address_add_imm_to_addr(ccu_address_handle res_addr, ccu_address_handle addr_a, uint16_t imm)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->address_add_imm_to_addr(res_addr, addr_a, imm));
    return ccu_result::CCU_SUCCESS;
}

// 参数加载类 相关接口
ccu_result ccu_load_arg(ccu_variable_handle var_handle, uint32_t arg_id)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->load_arg(var_handle, arg_id));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_load_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[ccu_load_var] invalid args, num[%u]", num);
        return ccu_result::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->load_var(addr, var_handle, num));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_load_var_from_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[ccu_load_var_from_var_addr] invalid args, num[%u]", num);
        return ccu_result::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->ccu_load_var_from_var_addr(addr_handle, var_handle, num));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_store_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[ccu_store_var] invalid args, num[%u]", num);
        return ccu_result::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->store_var(addr, var_handle, num));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_store_var_to_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    if (num == 0) {
        HCCL_ERROR("[ccu_store_var_to_var_addr] invalid args, num[%u]", num);
        return ccu_result::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->ccu_store_var_to_var_addr(addr_handle, var_handle, num));
    return ccu_result::CCU_SUCCESS;
}

// Event信号同步类 相关接口
ccu_result ccu_event_record(ccu_event_handle event_handle, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->event_record(event_handle, mask));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_event_wait(ccu_event_handle event_handle, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->event_wait(event_handle, mask));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_notify_record(ChannelHandle channel, uint32_t remote_notify_idx, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->notify_record(channel, remote_notify_idx, mask));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_notify_wait(ChannelHandle channel, uint32_t local_notify_idx, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->notify_wait(channel, local_notify_idx, mask));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_write_variable_with_notify(
    ChannelHandle channel, ccu_variable_handle var_handle, uint32_t remote_var_idx, uint32_t remote_notify_idx,
    uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->write_variable_with_notify(channel, var_handle, remote_var_idx, remote_notify_idx, mask));
    return ccu_result::CCU_SUCCESS;
}
// 0.5rtt 专用（V2）相关接口
ccu_result ccu_load_add_imm(
    ccu_variable_handle var_handle, uint16_t src_num, ccu_variable_handle offset_handle, uint16_t imm_add_value,
    ccu_variable_handle dst_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->load_add_imm(var_handle, src_num, offset_handle, imm_add_value, dst_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_add_imm_store(
    ccu_variable_handle var_handle, uint16_t dst_num, ccu_variable_handle offset_handle, uint16_t imm_add_value,
    ccu_variable_handle src_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->add_imm_store(var_handle, dst_num, offset_handle, imm_add_value, src_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_write_var_atomic_add(
    ccu_variable_handle channel_id_handle, ccu_remote_addr_handle var_addr_handle, ccu_variable_handle add_value_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(
        kernel->write_var_atomic_add(channel_id_handle, var_addr_handle, add_value_handle, event_handle, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_write_with_cnt_inc(
    ccu_variable_handle channel_id_handle, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_handle, ccu_remote_addr_handle in_cnt_addr_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(
        kernel->write_with_cnt_inc(channel_id_handle, remote_handle, local_handle, len_handle, in_cnt_addr_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_local_notify_record(const char* notify_tag, uint16_t mask)
{
    CCU_CHK_PTR_NULL_ASC(notify_tag);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_notify_record(notify_tag, mask));
    return ccu_result::CCU_SUCCESS;
}
ccu_result ccu_local_notify_wait(const char* notify_tag, uint16_t mask)
{
    CCU_CHK_PTR_NULL_ASC(notify_tag);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_notify_wait(notify_tag, mask));
    return ccu_result::CCU_SUCCESS;
}

// 本地数据拷贝 相关接口
ccu_result ccu_local_copy_mem_to_mem(
    ccu_local_addr_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, ccu_event_handle event,
    uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_copy_mem_to_mem(dst, src, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_local_copy_mem_to_buffer(
    ccu_buffer_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_copy_mem_to_buffer(dst, src, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_local_copy_buffer_to_mem(
    ccu_local_addr_handle dst, ccu_buffer_handle src, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_copy_buffer_to_mem(dst, src, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}
// 本地reduce 相关接口
ccu_result ccu_local_mem_reduce(
    ccu_local_addr_handle dst, ccu_local_addr_handle src, ccu_variable_handle len, HcclDataType data_type,
    HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->local_mem_reduce(dst, src, len, data_type, op_type, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_local_buffer_reduce(
    ccu_buffer_handle* buffers, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    if (buffers == nullptr || count == 0) {
        HCCL_ERROR("[ccu_local_buffer_reduce] invalid args, buffers[%p] count[%u]", buffers, count);
        return ccu_result::CCU_E_PARA;
    }
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(
        kernel->local_buffer_reduce(buffers, count, data_type, output_data_type, op_type, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

/* ========== 远端数据传输操作 ========== */
ccu_result ccu_read_mem_to_mem(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->read_mem_to_mem(channel, local_handle, remote_handle, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_read_mem_to_buffer(
    ChannelHandle channel, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->read_mem_to_buffer(channel, local_handle, remote_handle, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_read_mem_to_mem_reduce(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(
        kernel->read_mem_to_mem_reduce(channel, local_handle, remote_handle, len, data_type, op_type, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_write_mem_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_local_addr_handle local, ccu_variable_handle len,
    ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->write_mem_to_mem(channel, remote, local, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_write_buffer_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_buffer_handle local, ccu_variable_handle len,
    ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->write_buffer_to_mem(channel, remote, local, len, event, mask));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_write_mem_to_mem_reduce(
    ChannelHandle channel, ccu_remote_addr_handle remote, ccu_local_addr_handle local, ccu_variable_handle len,
    HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event, uint16_t mask)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->write_mem_to_mem_reduce(channel, remote, local, len, data_type, op_type, event, mask));
    return ccu_result::CCU_SUCCESS;
}

/* ========== 控制流操作 ========== */
ccu_result ccu_if_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->if_begin(var, immediate, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_if_else(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->if_else(label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_if_end(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->if_end(label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_flush_pending_ifs()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    kernel->flush_closable_pending_ifs();
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_while_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->while_begin(var, immediate, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_while_end(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->while_end(label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_do_while_begin(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->do_while_begin(label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_do_while_end(
    ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->do_while_end(var, immediate, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_if_begin_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->if_begin_var(lhs, rhs, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_while_begin_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->while_begin_var(lhs, rhs, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_do_while_end_var(
    ccu_variable_handle lhs, ccu_variable_handle rhs, ccu_condition_type cond_type, const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->do_while_end_var(lhs, rhs, cond_type, label));

    return ccu_result::CCU_SUCCESS;
}

/* ========== 函数调用操作 ========== */
ccu_result ccu_func_block_lookup(const void* func_ptr, uint64_t* out_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->func_block_lookup(func_ptr, out_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_func_block_begin(const void* func_ptr, uint64_t* out_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->func_block_begin(func_ptr, out_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_func_block_end(uint64_t handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->func_block_end(handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_func_define_in_arg(uint64_t handle, ccu_variable_handle formal)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->func_define_in_arg(handle, formal));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->func_call(handle, in_args, num_in));
    return ccu_result::CCU_SUCCESS;
}

/* ========== 循环操作 ========== */
ccu_result ccu_loop_create(ccu_loop* loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_create(loop));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_body_enter(ccu_loop loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_body_enter(loop));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_body_exit(ccu_loop loop)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_body_exit(loop));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_group_create(ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config)
{
    if (config == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    // 旧 config 无 var_offset，在边界归一化为 cfg（var_offset=0），kernel 只认 cfg。
    ccu_loop_group_cfg cfg{};
    ccu_loop_group_cfg_init(&cfg);
    cfg.clone_num = config->clone_num;
    cfg.clone_loop_offset = config->clone_loop_offset;
    cfg.addr_offset = config->addr_offset;
    cfg.ccu_buffer_offset = config->ccu_buffer_offset;
    cfg.event_offset = config->event_offset;
    cfg.var_offset = 0;
    return ccu_loop_group_create_cfg(group, max_loop_num, &cfg);
}

ccu_result ccu_loop_group_create_cfg(ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_cfg* cfg)
{
    if (cfg == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    CCU_CHK_RET_ASC(validate_ccu_cfg_header(&cfg->header, sizeof(ccu_loop_group_cfg), CCU_LOOPGROUP_CFG_VERSION));
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_group_create(group, max_loop_num, cfg));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_group_create_from_var(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var, ccu_variable_handle offset_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_group_create_from_var(group, max_loop_num, parallel_var, offset_var));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config)
{
    if (config == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    ccu_loop_cfg cfg{};
    ccu_loop_cfg_init(&cfg);
    cfg.addr_offset = config->addr_offset;
    cfg.iter_num = config->iter_num;
    return ccu_loop_group_add_loop_cfg(group, loop, &cfg);
}

ccu_result ccu_loop_group_add_loop_cfg(ccu_loop_group group, ccu_loop loop, const ccu_loop_cfg* cfg)
{
    if (cfg == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    CCU_CHK_RET_ASC(validate_ccu_cfg_header(&cfg->header, sizeof(ccu_loop_cfg), CCU_LOOP_CFG_VERSION));
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_group_add_loop(group, loop, cfg));
    return ccu_result::CCU_SUCCESS;
}

ccu_result ccu_loop_group_add_loop_from_var(ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    CCU_CHK_PTR_NULL_ASC(kernel);
    CCU_CHK_RET_ASC(kernel->loop_group_add_loop_from_var(group, loop, loop_param_var));
    return ccu_result::CCU_SUCCESS;
}

// 控制流标签栈 C 接口（_CcuIfStack* / _CcuDoWhileStack*）

void ccu_if_stack_push(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[ccu_if_stack_push] no current kernel, label=%s", label != nullptr ? label : "(null)");
        return;
    }
    kernel->if_label_stack_push(label);
}

void ccu_if_stack_mark_body_done()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[ccu_if_stack_mark_body_done] no current kernel");
        return;
    }
    kernel->if_label_stack_mark_body_done();
}

const char* ccu_if_stack_pop_for_else()
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[ccu_if_stack_pop_for_else] no current kernel");
        return nullptr;
    }
    return kernel->if_label_stack_pop_for_else();
}

void ccu_do_while_stack_push(const char* label)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto kernel = asc::ccu_kernel_mgr::get_instance(dev_logic_id).get_current_kernel();
    if (kernel == nullptr) {
        HCCL_ERROR("[ccu_do_while_stack_push] no current kernel, label=%s", label != nullptr ? label : "(null)");
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
