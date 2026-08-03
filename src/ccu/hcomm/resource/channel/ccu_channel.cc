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
    // 保留迁移前 channel->GetXxx() 的调用形式；当前对象本身已是 asc 本地值对象，不再包装 hcomm 指针。
    return this;
}

const CcuChannel *CcuChannel::operator->() const
{
    return this;
}

HcclResult CcuChannel::Init(ChannelHandle channel)
{
    ChannelEntity queryEntity{};
    queryEntity.abiHeader.version = HCOMM_CHANNEL_VERSION;
    queryEntity.abiHeader.magicWord = HCOMM_CHANNEL_MAGIC_WORD;
    queryEntity.abiHeader.size = sizeof(ChannelEntity);

    // 第一阶段不传数组地址，只查询 CKE、XN 和远端 Buffer 所需的元素数量。
    HcommResult ret = HcommCcuChannelGetPod(channel, &queryEntity);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    const uint32_t resourceNum = queryEntity.localNotifyNum;
    if (resourceNum == 0 || resourceNum != queryEntity.remoteNotifyNum ||
        resourceNum != queryEntity.ccuInfo.localXnNum || resourceNum != queryEntity.ccuInfo.remoteXnNum ||
        queryEntity.remoteBufferNum != 1) {
        return HCCL_E_INTERNAL;
    }

    // 资源数在同一个 Channel 快照中必须一一对应；先校验数量，再按精确容量分配本地容器。
    try {
        localCkes_.resize(queryEntity.localNotifyNum);
        remoteCkes_.resize(queryEntity.remoteNotifyNum);
        remoteBuffers_.resize(queryEntity.remoteBufferNum);
        localXns_.resize(queryEntity.ccuInfo.localXnNum);
        remoteXns_.resize(queryEntity.ccuInfo.remoteXnNum);
    } catch (const std::bad_alloc &) {
        return HCCL_E_MEMORY;
    }

    // 第二阶段把本地容器作为输出缓冲区交给 hcomm。hcomm 只在调用期间借用这些地址。
    queryEntity.localNotifyNum = static_cast<uint32_t>(localCkes_.size());
    queryEntity.remoteNotifyNum = static_cast<uint32_t>(remoteCkes_.size());
    queryEntity.remoteBufferNum = static_cast<uint32_t>(remoteBuffers_.size());
    queryEntity.localNotifyAddr = localCkes_.data();
    queryEntity.remoteNotifyAddr = remoteCkes_.data();
    queryEntity.remoteBufferAddr = remoteBuffers_.data();
    queryEntity.ccuInfo.localXnCapacity = static_cast<uint32_t>(localXns_.size());
    queryEntity.ccuInfo.remoteXnCapacity = static_cast<uint32_t>(remoteXns_.size());
    queryEntity.ccuInfo.localXnAddr = localXns_.data();
    queryEntity.ccuInfo.remoteXnAddr = remoteXns_.data();
    ret = HcommCcuChannelGetPod(channel, &queryEntity);
    if (ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(ret);
    }
    if (queryEntity.localNotifyNum != resourceNum || queryEntity.remoteNotifyNum != resourceNum ||
        queryEntity.ccuInfo.localXnNum != resourceNum || queryEntity.ccuInfo.remoteXnNum != resourceNum ||
        queryEntity.remoteBufferNum != 1) {
        return HCCL_E_INTERNAL;
    }
    for (const auto &cke : localCkes_) {
        if (cke.type != REGED_NOTIFY_CCU_CKE) {
            return HCCL_E_INTERNAL;
        }
    }
    for (const auto &cke : remoteCkes_) {
        if (cke.type != REGED_NOTIFY_CCU_CKE) {
            return HCCL_E_INTERNAL;
        }
    }
    if (remoteBuffers_[0].type != REGED_BUFFER_RMA ||
        remoteBuffers_[0].bufferInfo.rma.protectionInfo.type != PROTECTION_TYPE_UB ||
        remoteBuffers_[0].bufferInfo.rma.size == 0 || remoteBuffers_[0].bufferInfo.rma.size > UINT32_MAX) {
        return HCCL_E_INTERNAL;
    }

    // 变长数据已经由 vector 持有，标量快照中的借用指针必须清空，避免后续误认为其拥有内存。
    channelEntity_ = queryEntity;
    channelEntity_.localNotifyAddr = nullptr;
    channelEntity_.remoteNotifyAddr = nullptr;
    channelEntity_.remoteBufferAddr = nullptr;
    channelEntity_.ccuInfo.localXnAddr = nullptr;
    channelEntity_.ccuInfo.remoteXnAddr = nullptr;
    channelEntity_.ccuInfo.localXnCapacity = 0;
    channelEntity_.ccuInfo.remoteXnCapacity = 0;
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

HcclResult CcuChannel::GetLocCkeByIndex(uint32_t index, uint32_t &localCkeId) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= localCkes_.size()) {
        return HCCL_E_PARA;
    }
    localCkeId = localCkes_[index].notifyInfo.ccuCke.ckeId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= localXns_.size()) {
        return HCCL_E_PARA;
    }
    localXnId = localXns_[index];
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= remoteCkes_.size()) {
        return HCCL_E_PARA;
    }
    remoteCkeId = remoteCkes_[index].notifyInfo.ccuCke.ckeId;
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (index >= remoteXns_.size()) {
        return HCCL_E_PARA;
    }
    remoteXnId = remoteXns_[index];
    return HCCL_SUCCESS;
}

HcclResult CcuChannel::GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const
{
    if (result_ != HCCL_SUCCESS) {
        return result_;
    }
    if (remoteBuffers_.size() != 1) {
        return HCCL_E_INTERNAL;
    }
    const auto &remoteBuffer = remoteBuffers_[0].bufferInfo.rma;
    addr = remoteBuffer.addr;
    size = static_cast<uint32_t>(remoteBuffer.size);
    tokenId = remoteBuffer.protectionInfo.memInfo.ub.tokenId;
    tokenValue = remoteBuffer.protectionInfo.memInfo.ub.tokenValue;
    return HCCL_SUCCESS;
}

} // namespace hcomm
