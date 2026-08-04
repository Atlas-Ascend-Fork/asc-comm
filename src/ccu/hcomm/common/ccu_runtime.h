/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file ccu_runtime.h
 * \brief asc-comm CCU 数据面本地 runtime 封装。
 *
 * 解耦原则（公开为主，ABI 兜底）：
 *   - device 切换 / 设备内存分配释放：直接调用公开 aclrt* C API。
 *   - UB token 查询 / task-stream id：CANN 无公开等价，经 hcomm C ABI（HcommQueryUbToken /
 *     HcommGetTaskIdAndStreamId），参数全 POD。
 *   - 不再 include hcomm 的 adapter_rts_common.h / hcom_common.h / hcomm_adapter_rts.h 私有头。
 */

#ifndef ASCCOMM_CCU_RUNTIME_H
#define ASCCOMM_CCU_RUNTIME_H

#include <cstdint>

#include "ccu/hcomm/ccu_types.h"   // CcuResult

namespace hcomm {

/*!
 * \brief 获取当前线程绑定的设备逻辑 ID（封装公开 aclrtGetDevice）。
 * \return 设备逻辑 ID；失败返回 -1。
 */
int32_t AsccommGetDevice();

/*!
 * \brief 切换当前线程绑定的设备（封装公开 aclrtSetDevice）。
 * \return CcuResult::CCU_SUCCESS 成功。
 */
CcuResult AsccommSetDevice(int32_t deviceLogicId);

/*!
 * \brief 获取当前 runtime 上下文的 task id 与 stream id（经 pkg_inc ABI 兜底）。
 * \return CcuResult::CCU_SUCCESS 成功。
 */
CcuResult AsccommGetTaskIdAndStreamId(uint32_t &taskId, uint32_t &streamId);

/*!
 * \brief 查询 UB 内存 process token（经 pkg_inc ABI 兜底，CANN 无公开等价）。
 * \param[in] va 虚拟地址。
 * \param[in] size 地址区间长度（字节）。
 * \param[out] tokenId token id。
 * \param[out] tokenValue token value。
 * \return CcuResult::CCU_SUCCESS 成功。
 */
CcuResult AsccommQueryUbToken(uint64_t va, uint64_t size, uint32_t &tokenId, uint32_t &tokenValue);

/*!
 * \brief RAII 设备内存 buffer，构造/析构只调用公开 aclrtMalloc/aclrtFree。
 *        管理 CCU token 查询等场景分配的设备内存（如 1 字节 HBM）。
 */
class AsccommDevBuffer {
public:
    AsccommDevBuffer() = default;
    ~AsccommDevBuffer();

    AsccommDevBuffer(const AsccommDevBuffer &) = delete;
    AsccommDevBuffer &operator=(const AsccommDevBuffer &) = delete;
    AsccommDevBuffer(AsccommDevBuffer &&other) noexcept;
    AsccommDevBuffer &operator=(AsccommDevBuffer &&other) noexcept;

    /*!
     * \brief 分配设备内存。
     * \param[in] size 字节数。
     * \return CcuResult::CCU_SUCCESS 成功。
     */
    CcuResult Alloc(uint64_t size);

    /*!
     * \brief 释放设备内存（可重复调用）。
     */
    void Reset();

    /*!
     * \brief 获取设备内存地址；未分配返回 nullptr。
     */
    void *Get() const { return ptr_; }

private:
    void *ptr_{nullptr};
};

} // namespace hcomm

#endif // ASCCOMM_CCU_RUNTIME_H
