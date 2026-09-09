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

namespace asc {
CcuTransport::~CcuTransport() {}
CcuConnection::~CcuConnection() {}
// key function 实现（生成 vtable/typeinfo）
HcclResult CcuUrmaChannel::init() { return HCCL_SUCCESS; }
ChannelStatus CcuUrmaChannel::GetStatus() { return ChannelStatus{}; }
HcclResult CcuUrmaChannel::GetNotifyNum(uint32_t* notifyNum) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::GetRemoteMems(uint32_t* memNum, CommMem** remoteMem, char*** memInfos)
{
    return HCCL_SUCCESS;
}
HcclResult CcuUrmaChannel::UpdateMemInfo(HcommMemHandle* memHandles, uint32_t memHandleNum) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Clean() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Resume() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::ChannelFence() { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::notify_record(const uint32_t remote_notify_idx) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::notify_wait(const uint32_t local_notify_idx, const uint32_t timeout) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::WriteWithNotify(
    void* dst_, const void* src_, const uint64_t len_, uint32_t remote_notify_idx)
{
    return HCCL_SUCCESS;
}
HcclResult CcuUrmaChannel::Write(void* dst_, const void* src_, uint64_t len_) { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::Read(void* dst_, const void* src_, uint64_t len_) { return HCCL_SUCCESS; }
CcuUrmaChannel::CcuUrmaChannel(const EndpointHandle locEndpointHandle, const HcommChannelDesc& channelDesc) {}
uint32_t CcuUrmaChannel::get_die_id() const { return 0; }
uint32_t CcuUrmaChannel::get_channel_id() const { return 0; }
HcclResult CcuUrmaChannel::get_rmt_cke_by_index(const uint32_t index, uint32_t& rmt_cke_id_) const
{
    return HCCL_SUCCESS;
}
HcclResult CcuUrmaChannel::get_rmt_xn_by_index(const uint32_t index, uint32_t& rmt_xn_id_) const
{
    return HCCL_SUCCESS;
}
HcclResult CcuUrmaChannel::get_loc_cke_by_index(const uint32_t index, uint32_t& loc_cke_id) const
{
    return HCCL_SUCCESS;
}
HcclResult CcuUrmaChannel::get_loc_xn_by_index(const uint32_t index, uint32_t& loc_xn_id) const { return HCCL_SUCCESS; }
HcclResult CcuUrmaChannel::get_rmt_buffer(
    uint64_t& addr_, uint32_t& size, uint32_t& token_id, uint32_t& token_value) const
{
    addr_ = 0x1000;
    size = 256;
    token_id = 1;
    token_value = 100;
    return HCCL_SUCCESS;
}
// Channel key function
HcclResult Channel::UpdateMemInfo(HcommMemHandle* memHandles, uint32_t memHandleNum) { return HCCL_SUCCESS; }
HcommChannelKind Channel::GetChannelKind() const { return HcommChannelKind{}; }
HcclResult Channel::Serialize(std::shared_ptr<hccl::DeviceMem>& out) { return HCCL_SUCCESS; }
void Channel::AddPtrArrayDevMem(std::shared_ptr<hccl::DeviceMem> ptrArrayMem) {}
} // namespace asc
