/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_runtime.h"

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/hcomm_ccu_runtime_abi_api.h"   // C ABI: HcommQueryUbToken / HcommGetTaskIdAndStreamId

namespace hcomm {

namespace {
constexpr int32_t ASCCOMM_INVALID_DEVICE = -1;

CcuResult AclRetToCcuResult(aclError ret)
{
    return (ret == ACL_SUCCESS) ? CcuResult::CCU_SUCCESS : CcuResult::CCU_E_RUNTIME;
}
} // namespace

int32_t AsccommGetDevice()
{
    int32_t deviceId = ASCCOMM_INVALID_DEVICE;
    aclError ret = aclrtGetDevice(&deviceId);
    if (ret != ACL_SUCCESS) {
        HCCL_ERROR("[AsccommGetDevice] aclrtGetDevice failed, ret[%d].", static_cast<int>(ret));
        return ASCCOMM_INVALID_DEVICE;
    }
    return deviceId;
}

CcuResult AsccommSetDevice(int32_t deviceLogicId)
{
    aclError ret = aclrtSetDevice(deviceLogicId);
    if (ret != ACL_SUCCESS) {
        HCCL_ERROR("[AsccommSetDevice] aclrtSetDevice failed, deviceLogicId[%d] ret[%d].", deviceLogicId,
            static_cast<int>(ret));
        return AclRetToCcuResult(ret);
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult AsccommGetTaskIdAndStreamId(uint32_t &taskId, uint32_t &streamId)
{
    taskId = 0;
    streamId = 0;
    HcommResult ret = HcommGetTaskIdAndStreamId(&taskId, &streamId);
    if (ret != static_cast<HcommResult>(0)) {
        HCCL_ERROR("[AsccommGetTaskIdAndStreamId] HcommGetTaskIdAndStreamId failed, ret[%d].",
            static_cast<int>(ret));
        return CcuResult::CCU_E_RUNTIME;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult AsccommQueryUbToken(uint64_t va, uint64_t size, uint32_t &tokenId, uint32_t &tokenValue)
{
    tokenId = 0;
    tokenValue = 0;
    // token 信息属安全信息，不打印 va/size
    HcommResult ret = HcommQueryUbToken(va, size, &tokenId, &tokenValue);
    if (ret != static_cast<HcommResult>(0)) {
        HCCL_ERROR("[AsccommQueryUbToken] HcommQueryUbToken failed, ret[%d].", static_cast<int>(ret));
        return CcuResult::CCU_E_RUNTIME;
    }
    return CcuResult::CCU_SUCCESS;
}

AsccommDevBuffer::~AsccommDevBuffer()
{
    Reset();
}

AsccommDevBuffer::AsccommDevBuffer(AsccommDevBuffer &&other) noexcept : ptr_(other.ptr_)
{
    other.ptr_ = nullptr;
}

AsccommDevBuffer &AsccommDevBuffer::operator=(AsccommDevBuffer &&other) noexcept
{
    if (this != &other) {
        Reset();
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    return *this;
}

CcuResult AsccommDevBuffer::Alloc(uint64_t size)
{
    if (size == 0U) {
        HCCL_ERROR("[AsccommDevBuffer::Alloc] size must not be 0.");
        return CcuResult::CCU_E_PARA;
    }
    Reset();
    void *devPtr = nullptr;
    // 仅调用公开 aclrtMalloc / aclrtFree
    aclError ret = aclrtMalloc(&devPtr, static_cast<size_t>(size), ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        HCCL_ERROR("[AsccommDevBuffer::Alloc] aclrtMalloc failed, size[%llu] ret[%d].",
            static_cast<unsigned long long>(size), static_cast<int>(ret));
        return AclRetToCcuResult(ret);
    }
    ptr_ = devPtr;
    return CcuResult::CCU_SUCCESS;
}

void AsccommDevBuffer::Reset()
{
    if (ptr_ != nullptr) {
        (void)aclrtFree(ptr_);
        ptr_ = nullptr;
    }
}

} // namespace hcomm
