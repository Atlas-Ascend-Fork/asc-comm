/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RES_CONTAINER_H
#define CCU_RES_CONTAINER_H

#include <array>
#include <memory>
#include <vector>

#include "ccu_types.h"
#include "ccu_res_pack.h"
#include "ccu_provider_internal.h"

namespace hcomm {

/**
 * @note 职责：管理通信域持有的CCU资源
 */
class CcuInstance {
public:
    CcuInstance() = default;
    ~CcuInstance();
    CcuResult Init(const AscCommCcuInstanceCreateParam &param);
    CcuResult Reset();
    CcuResPack *GetResPack();
    CcuResult SaveKernel(const CcuKernelHandle kernelHandle);
    const std::vector<CcuKernelHandle> &GetUntranslatedKernels();

    CcuResult BeginRegister();
    CcuResult CheckRegistering() const;
    CcuResult EndRegister();
    void AbortRegister();

    int32_t GetDeviceLogicId() const;
    bool GetDieEnabled(uint32_t dieId) const;
    uint64_t GetXnBaseAddress(uint32_t dieId) const;
    uint32_t GetMissionKey(uint32_t dieId) const;
    uint32_t GetLoopChannelId(uint32_t sourceDieId, uint32_t destinationDieId) const;
    const AscCommCcuTokenInfo &GetResourceSpaceToken(uint32_t dieId) const;
    const AscCommCcuTokenInfo &GetDeviceMemoryToken() const;

private:
    enum class RegisterState { IDLE, REGISTERING, REGISTER_ABORTED };
    RegisterState registerState_{RegisterState::IDLE};

    int32_t devLogicId_{INT32_MAX};
    std::array<bool, ASCCOMM_CCU_MAX_DIE_NUM> dieEnabled_{};
    std::array<uint64_t, ASCCOMM_CCU_MAX_DIE_NUM> xnBaseAddress_{};
    std::array<uint32_t, ASCCOMM_CCU_MAX_DIE_NUM> missionKey_{};
    std::array<std::array<uint32_t, ASCCOMM_CCU_MAX_DIE_NUM>, ASCCOMM_CCU_MAX_DIE_NUM> loopChannelId_{};
    std::array<AscCommCcuTokenInfo, ASCCOMM_CCU_MAX_DIE_NUM> resourceSpaceToken_{};
    AscCommCcuTokenInfo deviceMemoryToken_{};
    std::unique_ptr<CcuResPack> resPack_{};
    std::vector<CcuKernelHandle> kernelHandles_{};
    std::vector<CcuKernelHandle> untranslatedKernelHandles_{};
};

} // namespace hcomm
#endif // CCU_RES_CONTAINER_H
