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
 * @note 句柄生命周期与 insHandle 绑定，无显式释放接口，由 HcommCcuInsDestroy 统一解映射并还池。
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
 * @note CKE 池与 kernel 自身的完成事件共用且单 die 容量有限，大 num 可能总量足够却无连续块。
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
 * @note 地址已在 Alloc 阶段映射并缓存，此处为纯查表；VA 供 CCU 外模块访问，有效期同句柄。
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
 * @brief 创建空白 CCU 资源描述符。
 * @param[in] dieId CCU 资源描述符归属的 ioDie ID。
 * @param[out] resDesc 创建成功后返回的资源描述符句柄，不可为 nullptr。
 * @return CcuResult。
 */

/**
 * @brief 为指定设备上的通信域建立 kernel 注册上下文。
 *
 * 由 hcomm 在建立通信域时调用。本接口只分配一个 CcuInsHandle 并建立数据面注册状态槽位，
 * 不拉起驱动、不申请硬件资源——二者属控制面，由 hcomm 的 CCU feature 与 RegisterContext 负责。
 *
 * deviceLogicId 必须显式传入：CcuInsHandle 由 per-device 计数器分配且各自从 0 起，
 * 跨设备会重号，仅凭 handle 无法定位注册上下文，也不得依赖线程当前设备推断。
 *
 * @param[in] deviceLogicId 目标设备逻辑 ID。
 * @param[out] handle 成功后返回的注册上下文句柄，不可为 nullptr。
 * @return CcuResult。
 */
extern CcuResult HcommCcuKernelRegistryCreate(int32_t deviceLogicId, CcuInsHandle* handle);

/**
 * @brief 销毁指定设备上的 kernel 注册上下文。
 *
 * 注销该上下文名下已注册的全部 kernel 并归还其指令区间，然后丢弃资源快照。
 * 调用方需保证目标设备可被切换，本接口内部会切换并恢复设备上下文。
 *
 * @param[in] handle 注册上下文句柄。
 * @param[in] deviceLogicId 上下文所属设备逻辑 ID。
 * @return CcuResult。
 */
extern CcuResult HcommCcuKernelRegistryDestroy(CcuInsHandle handle, int32_t deviceLogicId);

/**
 * @brief 查询指定设备上的 kernel 注册上下文是否存在。
 * @param[in] handle 注册上下文句柄。
 * @param[in] deviceLogicId 上下文所属设备逻辑 ID。
 * @param[out] exists 存在回填 1，不存在回填 0，不可为 nullptr。句柄不存在不视为错误。
 * @return CcuResult。
 */
extern CcuResult HcommCcuKernelRegistryExists(CcuInsHandle handle, int32_t deviceLogicId, uint32_t* exists);

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
extern CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t* tokenInfo);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_RES_H
