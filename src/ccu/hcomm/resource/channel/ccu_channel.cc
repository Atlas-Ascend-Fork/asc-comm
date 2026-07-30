/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/channel/ccu_channel.h"

#include <new>

namespace hcomm {

CcuChannel::CcuChannel(ChannelHandle channel)
{
    result_ = Init(channel);
}

CcuChannel *CcuChannel::operator->()
{
    return this;
}

const CcuChannel *CcuChannel::operator->() const
{
    return this;
}

HcclResult CcuChannel::Init(ChannelHandle channel)
{
    HcommResult ret = HcommCcuChannelEntityInit(&channelEntity_);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }

    uint32_t resourceNum = 0;
    ret = HcommCcuChannelGetPod(channel, &channelEntity_, nullptr, &resourceNum);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    if (resourceNum != channelEntity_.localNotifyNum || resourceNum != channelEntity_.remoteNotifyNum ||
        resourceNum != channelEntity_.sqNum || resourceNum != channelEntity_.cqNum) {
        return HCCL_E_INTERNAL;
    }

    try {
        resources_.resize(resourceNum);
    } catch (const std::bad_alloc &) {
        return HCCL_E_MEMORY;
    }

    uint32_t capacity = resourceNum;
    ret = HcommCcuChannelGetPod(channel, &channelEntity_, resources_.data(), &capacity);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    if (capacity != resourceNum || channelEntity_.localNotifyNum != resourceNum ||
        channelEntity_.remoteNotifyNum != resourceNum || channelEntity_.sqNum != resourceNum ||
        channelEntity_.cqNum != resourceNum) {
        resources_.clear();
        return HCCL_E_INTERNAL;
    }
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetResult() const
{
    return result_;
}

uint32_t CcuChannel::GetDieId() const
{
    return channelEntity_.ccuInfo.dieId;
}

uint32_t CcuChannel::GetChannelId() const
{
    return channelEntity_.ccuInfo.channelId;
}

HcclResult CcuChannel::GetResource(uint32_t index, const HcommCcuChannelResourcePod *&resource) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= resources_.size()) {
        return HCCL_E_PARA;
    }
    resource = &resources_[index];
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetLocCkeByIndex(uint32_t index, uint32_t &localCkeId) const
{
    const HcommCcuChannelResourcePod *resource = nullptr;
    HcclResult ret = GetResource(index, resource);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    localCkeId = resource->localCkeId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const
{
    const HcommCcuChannelResourcePod *resource = nullptr;
    HcclResult ret = GetResource(index, resource);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    localXnId = resource->localXnId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const
{
    const HcommCcuChannelResourcePod *resource = nullptr;
    HcclResult ret = GetResource(index, resource);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    remoteCkeId = resource->remoteCkeId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const
{
    const HcommCcuChannelResourcePod *resource = nullptr;
    HcclResult ret = GetResource(index, resource);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    remoteXnId = resource->remoteXnId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    addr = channelEntity_.ccuInfo.remoteBufferAddr;
    size = channelEntity_.ccuInfo.remoteBufferSize;
    tokenId = channelEntity_.ccuInfo.remoteTokenId;
    tokenValue = channelEntity_.ccuInfo.remoteTokenValue;
    return HCCL_SUCCESS;
}

} // namespace hcomm
