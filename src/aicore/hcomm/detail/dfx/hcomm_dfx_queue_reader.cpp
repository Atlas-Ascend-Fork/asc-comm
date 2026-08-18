/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_dfx_channel_dump_impl.h"
#include "hcomm_dfx_queue_read_kernel.h"

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include <cstring>
#include <new>

namespace AscendC {
namespace HcommDfx {

namespace {
struct QueueReadContext {
    void *deviceResult = nullptr;
    aclrtStream stream = nullptr;
};
} // namespace

void *CreateQueueReader()
{
    auto *ctx = new (std::nothrow) QueueReadContext;
    if (ctx == nullptr) {
        Trace("new queue read context failed");
        return nullptr;
    }
    aclError ret = aclrtMalloc(&ctx->deviceResult, sizeof(HcommDfxQueueReadResult), ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtMalloc queue read result failed ret=%d", ret);
        delete ctx;
        return nullptr;
    }

    ret = aclrtCreateStream(&ctx->stream);
    if (ret != ACL_SUCCESS) {
        Trace("aclrtCreateStream queue read failed ret=%d", ret);
        (void)aclrtFree(ctx->deviceResult);
        delete ctx;
        return nullptr;
    }
    return ctx;
}

void DestroyQueueReader(void *reader)
{
    auto *ctx = reinterpret_cast<QueueReadContext *>(reader);
    if (ctx == nullptr) {
        return;
    }
    if (ctx->stream != nullptr) {
        (void)aclrtDestroyStream(ctx->stream);
    }
    if (ctx->deviceResult != nullptr) {
        (void)aclrtFree(ctx->deviceResult);
    }
    delete ctx;
}

bool ReadQueueEntry(void *reader, uint64_t addr, uint8_t *data, uint32_t size)
{
    auto *ctx = reinterpret_cast<QueueReadContext *>(reader);
    if (ctx == nullptr || addr == 0 || data == nullptr || size == 0 || size > HCOMM_DFX_QUEUE_ENTRY_BYTES) {
        return false;
    }

    HcommDfxQueueReadArgs args = {addr, reinterpret_cast<uint64_t>(ctx->deviceResult), size, 0};
    hcomm_dfx_queue_read_kernel<<<1, 0, ctx->stream>>>(args);
    aclError ret = aclrtSynchronizeStream(ctx->stream);

    HcommDfxQueueReadResult result;
    std::memset(&result, 0, sizeof(result));
    if (ret == ACL_SUCCESS) {
        ret = aclrtMemcpy(&result, sizeof(result), ctx->deviceResult, sizeof(result), ACL_MEMCPY_DEVICE_TO_HOST);
    }

    if (ret != ACL_SUCCESS || result.ret != HCOMM_DFX_QUEUE_READ_SUCCESS || result.size != size) {
        Trace("queue read failed addr=0x%llx ret=%d entryRet=%u size=%u expect=%u",
            static_cast<unsigned long long>(addr), ret, result.ret, result.size, size);
        return false;
    }

    std::memcpy(data, result.data, size);
    return true;
}

} // namespace HcommDfx
} // namespace AscendC
