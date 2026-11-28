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
#include <mutex>
#include <vector>

#include "ccu/ccu_api/ccu_types.h"
#include "ccu_api/resource/common/asc_ccu_res_snapshot.h"

namespace asc {

/**
 * @note 职责：按 HcommCcuInstanceEntity 持有通信域的数据面注册状态。
 *
 * 包含 kernel 注册轮次状态机、kernel 归属列表，以及该通信域的资源快照。
 * AscCcuResSnapshot 是 hcomm RegisterContext.instance_resources 的本地快照；
 * 资源实体归 hcomm RegisterContext 所有，asc-comm 仅在本地规划和消费快照。
 */
class ccu_kernel_registry {
public:
    ccu_kernel_registry() = default;
    ~ccu_kernel_registry();
    // deviceLogicId 由调用方显式传入，不从线程当前设备推断：HcommCcuInstanceEntity 由 per-device
    // 计数器分配且各自从 0 起，跨设备会重号，实例必须落在指定设备的 handle table 上。
    // 该值在 LoadRegisterContext 之前即需就绪，析构时注销 kernel 依赖它定位 CcuKernelMgr。
    ccu_result init(int32_t device_logic_id);
    // 调用前应完成资源分配；加载会按实体指针深拷贝资源视图，期间实体及其指针目标不得变化。
    ccu_result load_register_context(const HcommCcuInstanceEntity& instance);
    ccu_result reset();
    asc_ccu_res_snapshot* get_res_snapshot();
    ccu_result save_kernel(const ccu_kernel_handle kernel_handle);
    const std::vector<ccu_kernel_handle>& get_untranslated_kernels();
    void set_handle(HcommCcuInstanceEntity ins_handle);

    ccu_result begin_register();
    ccu_result check_registering() const;
    ccu_result end_register();
    void abort_register();

private:
    enum class register_state { idle, registering, register_aborted };
    register_state register_state_{register_state::idle};

    int32_t dev_logic_id_{INT32_MAX};
    // CcuVarEventResMgr 按 handle 记账，析构时需以此调用 ReleaseByInstance。
    HcommCcuInstanceEntity ins_handle_{};
    std::unique_ptr<asc_ccu_res_snapshot> res_snapshot_{};
    // 资源接口可能在首次 kernel 注册前加载 Instance；后续 <<<>>> 注册轮复用该快照，避免重复加载。
    // 该锁仅串行化首次快照加载；同一实例的注册状态和资源申请状态仍要求调用方串行使用。
    std::mutex load_mutex_{};
    bool context_loaded_{false};
    std::vector<ccu_kernel_handle> kernel_handles_{};
    std::vector<ccu_kernel_handle> untranslated_kernel_handles_{};
};

} // namespace asc
#endif // CCU_KERNEL_REGISTRY_H
