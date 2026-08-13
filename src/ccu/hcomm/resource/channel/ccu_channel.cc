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

#include <cstdint>
#include <limits>

namespace hcomm {

CcuChannel::CcuChannel(ChannelHandle channel)
{
    result_ = Init(channel);
}

CcuChannel *CcuChannel::operator->()
{
    // 保留迁移前 channel->GetXxx() 的调用形式；当前对象本身已是 asc 本地值对象，不再包装 hcomm 指针。
    return this;
}

const CcuChannel *CcuChannel::operator->() const
{
    return this;
}

HcclResult CcuChannel::Init(ChannelHandle channel)
{
    HcommCcuChannelPod queryPod{};
    queryPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    queryPod.header.magicWord = HCOMM_CCU_CHANNEL_MAGIC_WORD;
    queryPod.header.size = sizeof(HcommCcuChannelPod);

    HcommResult ret = HcommCcuChannelGetPod(channel, &queryPod);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    if (queryPod.xnRange.count == 0 || queryPod.ckeRange.count == 0 || queryPod.rmtCcuBufSize == 0) {
        return HCCL_E_INTERNAL;
    }
    channelPod_ = queryPod;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetIdByRange(const HcommCcuIdRangePod &range, uint32_t index, uint32_t &id) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= range.count) {
        return HCCL_E_PARA;
    }
    if (range.startId > std::numeric_limits<uint32_t>::max() - index) {
        return HCCL_E_INTERNAL;
    }
    id = range.startId + index;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetResult() const
{
    return result_;
}

uint32_t CcuChannel::GetDieId() const
{
    return channelPod_.dieId;
}

uint32_t CcuChannel::GetChannelId() const
{
    return channelPod_.channelId;
}

HcclResult CcuChannel::GetLocCkeByIndex(uint32_t index, uint32_t &localCkeId) const
{
    return GetIdByRange(channelPod_.ckeRange, index, localCkeId);
}

HcclResult CcuChannel::GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const
{
    return GetIdByRange(channelPod_.xnRange, index, localXnId);
}

HcclResult CcuChannel::GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const
{
    return GetIdByRange(channelPod_.ckeRange, index, remoteCkeId);
}

HcclResult CcuChannel::GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const
{
    return GetIdByRange(channelPod_.xnRange, index, remoteXnId);
}

HcclResult CcuChannel::GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    addr = channelPod_.rmtCcuBufAddr;
    size = channelPod_.rmtCcuBufSize;
    tokenId = channelPod_.rmtCcuBufTokenId;
    tokenValue = channelPod_.rmtCcuBufTokenValue;
    return HCCL_SUCCESS;
}

} // namespace hcomm
