/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_H

#include "hcomm_dfx_collect.h"
#include "hcomm_dfx_exception.h"
#include "hcomm_dfx_parse.h"

namespace AscendC {

inline int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
{
    if (kernelSymbol == nullptr) {
        std::printf("[HcommDfxDump] bind exception callback failed: kernelSymbol is null\n");
        std::fflush(stdout);
        return HCOMM_FAILED;
    }

    aclrtFuncHandle funcHandle = nullptr;
    aclrtBinHandle binHandle = nullptr;
    if (aclrtGetFuncBySymbol(kernelSymbol, &funcHandle) != ACL_SUCCESS ||
        aclrtFunctionGetBinary(funcHandle, &binHandle) != ACL_SUCCESS ||
        aclrtBinarySetExceptionCallback(binHandle, HcommDfx::ExceptionCallback, nullptr) != ACL_SUCCESS) {
        std::printf("[HcommDfxDump] bind exception callback failed\n");
        std::fflush(stdout);
        return HCOMM_FAILED;
    }

    HcommDfx::MarkExceptionCallbackBound();
    HcommDfx::ClearPendingException();
    // Runs once per process at application request; print unconditionally so the callback binding stays visible by default.
    std::printf("[HcommDfxDump] exception callback registered\n");
    std::fflush(stdout);
    return HCOMM_SUCCESS;
}

inline int32_t HcommChannelInfoDump(ChannelHandle channel)
{
    bool shouldDump = !HcommDfx::IsExceptionCallbackBound() || HcommDfx::HasPendingException();
    if (!shouldDump) {
        return HCOMM_SUCCESS;
    }

    HcommDfxChannelDumpData dump {};
    int32_t ret = HcommDfx::DumpChannelInfo(1U, &channel, &dump, static_cast<uint32_t>(sizeof(dump)));
    if (ret == HCOMM_SUCCESS) {
        HcommDfx::ClearPendingException();
    }
    HcommDfx::Trace("HcommChannelInfoDump ret=%d len=%u", ret, static_cast<uint32_t>(sizeof(dump)));
    return ret;
}

} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_H
