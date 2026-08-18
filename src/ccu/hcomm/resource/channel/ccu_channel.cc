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

#include "hcomm/common/ccu_device_context.h"

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
    // 组装带 ABI 头的查询 POD，通知 hcomm 按当前 ABI 版本填写快照
    HcommCcuChannelPod queryPod{};
    queryPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    queryPod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    queryPod.header.size = sizeof(HcommCcuChannelPod);

    // 通过控制面注入的函数表发起单次查询（不再分两阶段传数组地址）
    const auto channelQuery = GetCurrentCcuControlOps().channelQuery;
    if (channelQuery == nullptr) {
        return HCCL_E_INTERNAL;
    }
    int32_t ret = channelQuery(channel, &queryPod);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    // 远端 CCU buffer 尚未就绪时拒绝使用该 Channel
    if (queryPod.rmtCcuBufSize == 0) {
        return HCCL_E_INTERNAL;
    }
    // 快照整体拷贝到本地，后续算法只读这份 POD，不再触碰 hcomm 对象
    channelPod_ = queryPod;
    return HCCL_SUCCESS;
}

// 从 POD 定长数组取第 index 个资源 id：先校验快照查询结果，再校验数组与下标
HcclResult CcuChannel::GetIdByArray(const uint32_t *ids, uint32_t index, uint32_t &id) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (ids == nullptr) {
        return HCCL_E_PTR;
    }
    if (index >= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY) {
        return HCCL_E_PARA;
    }
    id = ids[index];
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
    return GetIdByArray(channelPod_.localCkeIds, index, localCkeId);
}

HcclResult CcuChannel::GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const
{
    return GetIdByArray(channelPod_.localXnIds, index, localXnId);
}

HcclResult CcuChannel::GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const
{
    return GetIdByArray(channelPod_.remoteCkeIds, index, remoteCkeId);
}

HcclResult CcuChannel::GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const
{
    return GetIdByArray(channelPod_.remoteXnIds, index, remoteXnId);
}

HcclResult CcuChannel::GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    // 远端 CCU 资源 buffer 的基址、长度与 token 已由 hcomm 在快照中按设备规格算好，这里直接返回
    addr = channelPod_.rmtCcuBufAddr;
    size = channelPod_.rmtCcuBufSize;
    tokenId = channelPod_.rmtCcuBufTokenId;
    tokenValue = channelPod_.rmtCcuBufTokenValue;
    return HCCL_SUCCESS;
}

} // namespace hcomm
