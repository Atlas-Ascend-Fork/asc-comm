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

#include "ccu_types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

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