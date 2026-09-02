/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/dfx/hcomm_channel_dump.h"
#include "hcomm_dfx_impl.h"
#include "../../common/hcomm_inner_def.h"

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include <cstdio>
#include <cstring>

extern "C" aclError aclrtGetFuncBySymbol(const void *symbol, aclrtFuncHandle *funcHandle);
extern "C" aclError aclrtFunctionGetBinary(aclrtFuncHandle funcHandle, aclrtBinHandle *binHandle);

namespace AscendC {

int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol)
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
    std::printf("[HcommDfxDump] exception callback registered\n");
    std::fflush(stdout);
    return HCOMM_SUCCESS;
}

int32_t HcommChannelInfoDump(ChannelHandle channel, HcommDfxChannelDumpData *dump)
{
    const bool callbackBound = HcommDfx::IsExceptionCallbackBound();
    if (callbackBound && !HcommDfx::HasPendingException()) {
        return HCOMM_SUCCESS;
    }
    if (callbackBound) {
        HcommDfx::ClearPendingException();
    }

    uint32_t dumpLen = 0;
    int32_t ret = HcommDfx::GetChannelDumpInfoSize(1U, &channel, &dumpLen);
    if (ret != HCOMM_SUCCESS || dumpLen == 0) {
        std::printf("[HcommDfxDump] get dump size failed ret=%d len=%u\n", ret, dumpLen);
        std::fflush(stdout);
        return ret;
    }

    HcommDfxChannelDumpData *dumpData = dump;
    void *dumpBuffer = dumpData;
    void *allocatedBuffer = nullptr;
    if (dumpData == nullptr) {
        aclError allocRet = aclrtMallocHost(&allocatedBuffer, dumpLen);
        if (allocRet != ACL_SUCCESS) {
            std::printf("[HcommDfxDump] allocate dump buffer failed ret=%d len=%u\n", allocRet, dumpLen);
            std::fflush(stdout);
            return HCOMM_FAILED;
        }
        if (allocatedBuffer == nullptr) {
            return HCOMM_FAILED;
        }
        dumpBuffer = allocatedBuffer;
        dumpData = reinterpret_cast<HcommDfxChannelDumpData *>(dumpBuffer);
    }

    std::memset(dumpBuffer, 0, dumpLen);
    ret = HcommDfx::DumpChannelInfo(1U, &channel, dumpBuffer, dumpLen);
    if (allocatedBuffer != nullptr) {
        (void)aclrtFreeHost(allocatedBuffer);
    }
    std::printf("[HcommDfxDump] HcommChannelInfoDump ret=%d len=%u\n", ret, dumpLen);
    std::fflush(stdout);
    return ret;
}

} // namespace AscendC
