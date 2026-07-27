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

#include "ccu_res_defs.h"
#include "ccu_types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @brief 创建空白 CCU 资源描述符。
 * @param[in] dieId CCU 资源描述符归属的 ioDie ID。
 * @param[out] resDesc 创建成功后返回的资源描述符句柄，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsResDescCreate(uint32_t dieId, HcommCcuResDescHandle *resDesc);

/**
 * @brief 销毁 CCU 资源描述符。
 * @param[in] resDesc 资源描述符句柄。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsResDescDestroy(HcommCcuResDescHandle resDesc);

/**
 * @brief 查询 CCU 资源描述符归属的 ioDie ID。
 * @param[in] resDesc 资源描述符句柄。
 * @param[out] dieId 查询成功后返回 ioDie ID，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsResDescQueryDieId(HcommCcuResDescHandle resDesc, uint32_t *dieId);

/**
 * @brief 设置指定 CCU 资源描述符内指定资源类型的资源数量。
 * @param[in] resDesc 资源描述符句柄。
 * @param[in] resType 资源类型。
 * @param[in] resNum 资源数量。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsResDescSetNum(
    HcommCcuResDescHandle resDesc, HcommCcuResType resType, uint32_t resNum);

/**
 * @brief 查询指定 CCU 资源描述符内指定资源类型的资源数量。
 * @param[in] resDesc 资源描述符句柄。
 * @param[in] resType 资源类型。
 * @param[out] resNum 查询成功后返回资源数量，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsResDescQueryNum(
    HcommCcuResDescHandle resDesc, HcommCcuResType resType, uint32_t *resNum);

/**
 * @brief 基于资源描述符创建 CCU 实例。
 * @param[in] resDescs 资源描述符句柄数组。
 * @param[in] resDescNum 资源描述符数量。
 * @param[out] ccuInsHandle 创建成功后返回的 CCU 实例句柄，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsCreate(
    const HcommCcuResDescHandle *resDescs, uint32_t resDescNum, CcuInsHandle *ccuInsHandle);

/**
 * @brief 使用指定 ioDie 上的默认资源创建 CCU 实例。
 * @param[in] dieIds ioDie ID 数组。
 * @param[in] dieNum ioDie 数量。
 * @param[out] ccuInsHandle 创建成功后返回的 CCU 实例句柄，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsCreateDefault(
    const uint32_t *dieIds, uint32_t dieNum, CcuInsHandle *ccuInsHandle);

/**
 * @brief 销毁 CCU 实例。
 * @param[in] ccuInsHandle CCU 实例句柄。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsDestroy(CcuInsHandle ccuInsHandle);

/**
 * @brief 查询 CCU 实例在资源描述符所属 ioDie 上占用的资源。
 * @param[in] ccuInsHandle CCU 实例句柄。
 * @param[in,out] resDesc 调用方基于待查询 ioDie 创建的资源描述符句柄，查询成功后写入该 ioDie 上的占用资源数量。
 * @return CcuResult。
 */
extern CcuResult HcommCcuInsQueryResDesc(CcuInsHandle ccuInsHandle, HcommCcuResDescHandle resDesc);

/**
 * @brief 查询资源描述符所属 ioDie 上的剩余 CCU 资源。
 * @param[in,out] resDesc 调用方基于待查询 ioDie 创建的资源描述符句柄，查询成功后写入该 ioDie 上的剩余资源数量。
 * @return CcuResult。
 */
extern CcuResult HcommCcuQueryRemainResDesc(HcommCcuResDescHandle resDesc);

/**
 * @brief 查询 CCU Kernel 的资源诉求。
 * @param[in] kernelFunc CCU Kernel 函数指针，不可为 nullptr。
 * @param[in] kernelArgs CCU Kernel 参数数组；argNum 为 0 时可为 nullptr。
 * @param[in] argNum CCU Kernel 参数数量。
 * @param[in,out] resDesc 调用方创建的资源描述符句柄，查询成功后写入资源诉求。
 * @return CcuResult。
 */
extern CcuResult HcommCcuKernelQueryResReq(
    const void *kernelFunc, const void **kernelArgs, uint32_t argNum, HcommCcuResDescHandle resDesc);

/**
 * @brief 查询/计算一段本端内存区域的 CCU 访问 token，供 host 端组装 LocalAddr/RemoteAddr 使用。
 * @param[in]  srcVa     内存区域起始虚地址（VA），必须为已注册的合法地址，不可为 0。
 * @param[in]  size      内存区域长度，单位：字节，必须 > 0 且不超过该 VA 对应映射的实际大小。
 * @param[out] tokenInfo 输出指针，指向单个 uint64_t（非数组），成功时写入计算得到的 token 值；不可为 nullptr。
 * @return CcuResult。CCU_SUCCESS 表示成功；CCU_E_PARA 表示 srcVa/size 为 0 或非法；
 *         CCU_E_PTR 表示 tokenInfo 为 nullptr；其余为底层驱动错误码。
 * @note 仅可在 host 端调用，不能在 kernel 函数体内调用。token 属安全信息，调用方不应打印；
 *       其生命周期与对应内存注册绑定。跨 rank 场景下本端 token 需经带外通道交换给对端组装 RemoteAddr。
 */
extern CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t *tokenInfo);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_RES_H
