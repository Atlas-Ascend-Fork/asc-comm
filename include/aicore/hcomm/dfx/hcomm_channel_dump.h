/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
#define ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H

#include "hcomm/hcomm_common.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "acl/acl.h"
#include "acl/acl_rt.h"

#ifdef __cplusplus
extern "C" {
#endif
aclError aclrtGetFuncBySymbol(const void *symbol, aclrtFuncHandle *funcHandle);
aclError aclrtFunctionGetBinary(aclrtFuncHandle funcHandle, aclrtBinHandle *binHandle);
#ifdef __cplusplus
}
#endif

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_CHANNEL_DUMP_H
#endif

#include "../../../../impl/adv_api/detail/hcomm/dfx/common/hcomm_dfx_impl.h"

namespace AscendC {

/*!
 * @brief Registers the exception callback for the target kernel.
 * @param [in] kernelSymbol Kernel symbol used to bind the exception callback.
 * @return 0 on success, -1 on failure.
 * @note Call this before launching the target kernel. Once the kernel triggers an AIC Error, the framework only
 *       records the exception state and does not perform D2H, printing, or launch operations inside the callback.
 */
inline int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
{
    if (kernelSymbol == nullptr) {
        std::printf("[HcommDfxDump] bind exception callback failed: kernelSymbol is null\n");
        std::fflush(stdout);
        return -1;
    }

    aclrtFuncHandle funcHandle = nullptr;
    aclrtBinHandle binHandle = nullptr;
    if (aclrtGetFuncBySymbol(kernelSymbol, &funcHandle) != ACL_SUCCESS ||
        aclrtFunctionGetBinary(funcHandle, &binHandle) != ACL_SUCCESS ||
        aclrtBinarySetExceptionCallback(binHandle, HcommDfx::ExceptionCallback, nullptr) != ACL_SUCCESS) {
        std::printf("[HcommDfxDump] bind exception callback failed\n");
        std::fflush(stdout);
        return -1;
    }

    HcommDfx::ClearPendingException();
    std::printf("[HcommDfxDump] exception callback registered\n");
    std::fflush(stdout);
    return 0;
}

/*!
 * @brief Dumps and prints the channel diagnostics at a host safe point after stream synchronization fails.
 * @param [in] channel Channel handle owned by the operator.
 * @param [in] usePendingGate Whether to keep the legacy pending-based gate. False means dump directly.
 * @return 0 on success, -1 on failure.
 * @note Safe to call unconditionally. When usePendingGate is true, this API keeps the legacy behavior and only dumps
 *       after an exception has been recorded. When it is false, the channel is dumped directly.
 */
inline int32_t HcommChannelInfoDump(ChannelHandle channel, bool usePendingGate = false)
{
    if (usePendingGate) {
        if (!HcommDfx::HasPendingException()) {
            return 0;
        }
        HcommDfx::ClearPendingException();
        std::printf("[HcommDfxDump] pending exception detected, dump channel info\n");
        std::fflush(stdout);
    } else {
        std::printf("[HcommDfxDump] dump channel info directly\n");
        std::fflush(stdout);
    }

    uint32_t dumpLen = 0;
    int32_t ret = HcommDfx::GetChannelDumpInfoSize(1U, &channel, &dumpLen);
    if (ret != 0 || dumpLen == 0) {
        std::printf("[HcommDfxDump] get dump size failed ret=%d len=%u\n", ret, dumpLen);
        std::fflush(stdout);
        return ret;
    }

    void *dumpBuffer = nullptr;
    aclError allocRet = aclrtMallocHost(&dumpBuffer, dumpLen);
    if (allocRet != ACL_SUCCESS) {
        std::printf("[HcommDfxDump] allocate dump buffer failed ret=%d len=%u\n", allocRet, dumpLen);
        std::fflush(stdout);
        return -1;
    }
    if (dumpBuffer == nullptr) {
        return -1;
    }

    std::memset(dumpBuffer, 0, dumpLen);
    ret = HcommDfx::DumpChannelInfo(1U, &channel, dumpBuffer, dumpLen);
    (void)aclrtFreeHost(dumpBuffer);
    std::printf("[HcommDfxDump] HcommChannelInfoDump ret=%d len=%u\n", ret, dumpLen);
    std::fflush(stdout);
    return ret;
}

} // namespace AscendC

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_CHANNEL_DUMP_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_CHANNEL_DUMP_H
#endif

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
