/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_KERNEL_REGISTRY_H
#define CCU_KERNEL_REGISTRY_H

#include <memory>
#include <vector>

#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/resource/common/asc_ccu_res_snapshot.h"

namespace asccomm {

/**
 * @note 职责：按 CcuInsHandle 持有通信域的数据面注册状态。
 *
 * 包含 kernel 注册轮次状态机、kernel 归属列表，以及该通信域的资源快照。
 * AscCcuResSnapshot 是 hcomm RegisterContext.instanceResources 的本地快照；
 * 资源实体归 hcomm RegisterContext 所有，asc-comm 仅在本地规划和消费快照。
 */
class CcuKernelRegistry {
public:
    CcuKernelRegistry() = default;
    ~CcuKernelRegistry();
    // deviceLogicId 由调用方显式传入，不从线程当前设备推断：CcuInsHandle 由 per-device
    // 计数器分配且各自从 0 起，跨设备会重号，实例必须落在指定设备的 handle table 上。
    // 该值在 LoadRegisterContext 之前即需就绪，析构时注销 kernel 依赖它定位 CcuKernelMgr。
    CcuResult Init(int32_t deviceLogicId);
    CcuResult LoadRegisterContext(const HcommCcuRegisterContextPod &context);
    CcuResult Reset();
    AscCcuResSnapshot* GetResSnapshot();
    CcuResult SaveKernel(const CcuKernelHandle kernelHandle);
    const std::vector<CcuKernelHandle>& GetUntranslatedKernels();
    void SetHandle(CcuInsHandle insHandle);

    CcuResult BeginRegister();
    CcuResult CheckRegistering() const;
    CcuResult EndRegister();
    void AbortRegister();

private:
    enum class RegisterState { IDLE, REGISTERING, REGISTER_ABORTED };
    RegisterState registerState_{RegisterState::IDLE};

    int32_t devLogicId_{INT32_MAX};
    // CcuVarEventResMgr 按 handle 记账，析构时需以此调用 ReleaseByInstance。
    CcuInsHandle insHandle_{0};
    std::unique_ptr<AscCcuResSnapshot> resSnapshot_{};
    std::vector<CcuKernelHandle> kernelHandles_{};
    std::vector<CcuKernelHandle> untranslatedKernelHandles_{};
};

} // namespace asccomm
#endif // CCU_KERNEL_REGISTRY_H
