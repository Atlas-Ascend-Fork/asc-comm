/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RES_H
#define CCU_RES_H

#include <stdint.h>

#include "ccu/hcomm/ccu_res_defs.h"
#include "ccu/hcomm/ccu_types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/** CCU 实例类型，决定通信域的算子展开模式与资源规格。 */
typedef enum {
    CCU_DEFAULT = 0,   /**< default, now is scheduler mode */
    CCU_SCHED = 1,     /**< scheduler mode, with less loop resource */
    CCU_MS = 2,        /**< memory slices mode, with more loop resouce */
    CCU_UNUSED = 254,
    CCU_RESERVED = 255 /**< reserved */
} CcuInstanceType;

/**
 * @brief 在指定 CCU 实例上预约一段连续的 Variable(XN) 资源，并在申请期完成地址映射。
 * @param[in] insHandle CCU 实例句柄，资源从该实例的资源池中切分。
 * @param[in] dieId 资源归属的 ioDie ID，取值范围 [0, CCU_MAX_IODIE_NUM)。
 * @param[in] num 预约的连续资源个数，须大于 0。
 * @param[out] varHandle 预约句柄，不可为 nullptr；失败时置 0。
 * @return CcuResult。CCU_E_PARA：dieId 或 num 非法；CCU_E_UNAVAIL：无满足 num 的连续块；
 *         CCU_E_PTR：varHandle 为 nullptr；CCU_E_RUNTIME：地址映射失败。
 */
extern CcuResult HcommCcuVariableAlloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuVariableHandle *varHandle);

/**
 * @brief 预约一段连续的 Event(CKE) 资源，参数与生命周期约束同 HcommCcuVariableAlloc。
 * @param[in] insHandle CCU 实例句柄。
 * @param[in] dieId 资源归属的 ioDie ID，取值范围 [0, CCU_MAX_IODIE_NUM)。
 * @param[in] num 预约的连续资源个数，须大于 0。
 * @param[out] eventHandle 预约句柄，不可为 nullptr；失败时置 0。
 * @return CcuResult，错误码语义同 HcommCcuVariableAlloc。
 */
extern CcuResult HcommCcuEventAlloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuEventHandle *eventHandle);

/**
 * @brief 查询预约句柄名下第 index 个 Variable(XN) 的进程可访问虚拟地址。
 * @param[in] varHandle HcommCcuVariableAlloc 返回的预约句柄。
 * @param[in] index 预约段内序号，取值范围 [0, num)。
 * @param[out] va 该资源的进程可访问虚拟地址，不可为 nullptr。
 * @return CcuResult。CCU_E_NOT_FOUND：句柄不存在；CCU_E_PARA：句柄类型不符或 index 越界；
 *         CCU_E_PTR：va 为 nullptr。
 */
extern CcuResult HcommCcuVariableGetAddr(CcuVariableHandle varHandle, uint32_t index, uint64_t *va);

/**
 * @brief 查询预约句柄名下第 index 个 Event(CKE) 的进程可访问虚拟地址。
 * @param[in] eventHandle HcommCcuEventAlloc 返回的预约句柄。
 * @param[in] index 预约段内序号，取值范围 [0, num)。
 * @param[out] va 该资源的进程可访问虚拟地址，不可为 nullptr。
 * @return CcuResult，错误码与约束同 HcommCcuVariableGetAddr。
 */
extern CcuResult HcommCcuEventGetAddr(CcuEventHandle eventHandle, uint32_t index, uint64_t *va);

/**
 * @brief 查询/计算一段本端内存区域的 CCU 访问 token，供 host 端组装 LocalAddr/RemoteAddr 使用。
 * @param[in] srcVa 内存区域起始虚地址（VA），必须为已注册的合法地址，不可为 0。
 * @param[in] size 内存区域长度，单位：字节，必须 > 0。
 * @param[out] tokenInfo 输出指针，成功时写入计算得到的 token 值；不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t* tokenInfo);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_RES_H
