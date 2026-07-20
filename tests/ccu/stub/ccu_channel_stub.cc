/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_urma_channel_compat.h"

namespace hcomm {
CcuTransport::~CcuTransport() {}
CcuConnection::~CcuConnection() {}
// key function 实现（生成 vtable/typeinfo）
HcclResult CcuUrmaChannel::Init() { return HCCL_SUCCESS; }
ChannelStatus CcuUrmaChannel::GetStatus() { return ChannelStatus{}; }
HcclResult CcuUrmaChannel::GetNotifyNum(uint32_t *notifyNum) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetRemoteMems(uint32_t *memNum, CommMem **remoteMem, char ***memInfos) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::UpdateMemInfo(HcommMemHandle *memHandles, uint32_t memHandleNum) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Clean() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Resume() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::ChannelFence() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::NotifyRecord(const uint32_t remoteNotifyIdx) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::NotifyWait(const uint32_t localNotifyIdx, const uint32_t timeout) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::WriteWithNotify(void *dst, const void *src, const uint64_t len, uint32_t remoteNotifyIdx) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Write(void *dst, const void *src, uint64_t len) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Read(void *dst, const void *src, uint64_t len) { return HCCL_SUCCESS; }
CcuUrmaChannel::CcuUrmaChannel(const EndpointHandle locEndpointHandle, const HcommChannelDesc &channelDesc) {}
uint32_t CcuUrmaChannel::GetDieId() const { return 0; }
uint32_t CcuUrmaChannel::GetChannelId() const { return 0; }
HcclResult CcuUrmaChannel::GetRmtCkeByIndex(const uint32_t index, uint32_t &rmtCkeId) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetRmtXnByIndex(const uint32_t index, uint32_t &rmtXnId) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetLocCkeByIndex(const uint32_t index, uint32_t &locCkeId) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetLocXnByIndex(const uint32_t index, uint32_t &locXnId) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const
{
    addr       = 0x1000;
    size       = 256;
    tokenId    = 1;
    tokenValue = 100;
    return HCCL_SUCCESS;
}
// Channel key function
HcclResult Channel::UpdateMemInfo(HcommMemHandle *memHandles, uint32_t memHandleNum) { return HCCL_SUCCESS; }
HcommChannelKind Channel::GetChannelKind() const { return HcommChannelKind{}; }
HcclResult Channel::Serialize(std::shared_ptr<hccl::DeviceMem> &out) { return HCCL_SUCCESS; }
void Channel::AddPtrArrayDevMem(std::shared_ptr<hccl::DeviceMem> ptrArrayMem) {}
} // namespace hcomm
