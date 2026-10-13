/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RESOURCE_API_H
#define CCU_RESOURCE_API_H

#include <stdint.h>

#include "ccu/hcomm/ccu_api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reserve contiguous variable (XN) resources on a CCU instance.
 * @param_[in] ins_handle CCU instance handle.
 * @param_[in] dieId IO die that owns the resources.
 * @param_[in] num_ Number of contiguous resources to reserve; must be greater than zero.
 * @param_[out] var_handle Reservation handle; set to zero on failure.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult asccomm_ccu_variable_alloc(
    CcuInsHandle ins_handle, uint8_t die_id, uint32_t num_, ccu_variable_handle* var_handle);

/**
 * @brief Reserve contiguous event (CKE) resources on a CCU instance.
 * @param_[in] ins_handle CCU instance handle.
 * @param_[in] dieId IO die that owns the resources.
 * @param_[in] num_ Number of contiguous resources to reserve; must be greater than zero.
 * @param_[out] event_handle Reservation handle; set to zero on failure.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult asccomm_ccu_event_alloc(
    CcuInsHandle ins_handle, uint8_t die_id, uint32_t num_, ccu_event_handle* event_handle);

/**
 * @brief Query the process-accessible address of a reserved variable resource.
 * @param_[in] var_handle Handle returned by asccomm_ccu_variable_alloc.
 * @param_[in] index Index within the reserved range.
 * @param_[out] va Process-accessible virtual address.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult asccomm_ccu_variable_get_addr(ccu_variable_handle var_handle, uint32_t index, uint64_t* va);

/**
 * @brief Query the process-accessible address of a reserved event resource.
 * @param_[in] event_handle Handle returned by asccomm_ccu_event_alloc.
 * @param_[in] index Index within the reserved range.
 * @param_[out] va Process-accessible virtual address.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult asccomm_ccu_event_get_addr(ccu_event_handle event_handle, uint32_t index, uint64_t* va);

/**
 * @brief Query the CCU access token for a local memory range.
 * @param_[in] src_va Start virtual address of a registered memory range.
 * @param_[in] size Size of the memory range in bytes; must be greater than zero.
 * @param_[out] token_info Calculated token.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult asccomm_ccu_get_mem_token(uint64_t src_va, uint64_t size, uint64_t* token_info);

#ifdef __cplusplus
}
#endif

#endif // CCU_RESOURCE_API_H
