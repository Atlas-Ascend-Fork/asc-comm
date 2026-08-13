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
 * @brief Reserve contiguous Variable (XN) resources on a CCU instance.
 * @param[in] insHandle CCU instance handle.
 * @param[in] dieId IO die that owns the resources.
 * @param[in] num Number of contiguous resources to reserve; must be greater than zero.
 * @param[out] varHandle Reservation handle; set to zero on failure.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult HcommCcuVariableAlloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuVariableHandle *varHandle);

/**
 * @brief Reserve contiguous Event (CKE) resources on a CCU instance.
 * @param[in] insHandle CCU instance handle.
 * @param[in] dieId IO die that owns the resources.
 * @param[in] num Number of contiguous resources to reserve; must be greater than zero.
 * @param[out] eventHandle Reservation handle; set to zero on failure.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult HcommCcuEventAlloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuEventHandle *eventHandle);

/**
 * @brief Query the process-accessible address of a reserved Variable resource.
 * @param[in] varHandle Handle returned by HcommCcuVariableAlloc.
 * @param[in] index Index within the reserved range.
 * @param[out] va Process-accessible virtual address.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult HcommCcuVariableGetAddr(CcuVariableHandle varHandle, uint32_t index, uint64_t *va);

/**
 * @brief Query the process-accessible address of a reserved Event resource.
 * @param[in] eventHandle Handle returned by HcommCcuEventAlloc.
 * @param[in] index Index within the reserved range.
 * @param[out] va Process-accessible virtual address.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult HcommCcuEventGetAddr(CcuEventHandle eventHandle, uint32_t index, uint64_t *va);

/**
 * @brief Query the CCU access token for a local memory range.
 * @param[in] srcVa Start virtual address of a registered memory range.
 * @param[in] size Size of the memory range in bytes; must be greater than zero.
 * @param[out] tokenInfo Calculated token.
 * @return CCU_SUCCESS on success; otherwise a CcuResult error code.
 */
extern CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t *tokenInfo);

#ifdef __cplusplus
}
#endif

#endif // CCU_RESOURCE_API_H
