/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_CCU_URMA_CHANNEL_COMPAT_H
#define ASCCOMM_CCU_UT_CCU_URMA_CHANNEL_COMPAT_H

#include <memory>

#include "hcomm/hcomm_types.h"

#include "hcomm_primitives.h"

namespace hccl {
class DeviceMem;
}

namespace asc {
using EndpointHandle = void*;
struct HcommChannelDesc {};
struct HcommMemHandle {};
struct CommMem {};
struct ChannelStatus {};
enum class HcommChannelKind { invalid };

class CcuTransport {
public:
    virtual ~CcuTransport();
};
class CcuConnection {
public:
    virtual ~CcuConnection();
};

class Channel {
public:
    virtual ~Channel() = default;
    virtual HcclResult UpdateMemInfo(HcommMemHandle* memHandles, uint32_t memHandleNum);
    virtual HcommChannelKind GetChannelKind() const;
    virtual HcclResult Serialize(std::shared_ptr<hccl::DeviceMem>& out);
    virtual void AddPtrArrayDevMem(std::shared_ptr<hccl::DeviceMem> ptrArrayMem);
};

class CcuUrmaChannel : public Channel {
public:
    CcuUrmaChannel(EndpointHandle locEndpointHandle, const HcommChannelDesc& channelDesc);
    ~CcuUrmaChannel() override = default;
    HcclResult init();
    ChannelStatus GetStatus();
    HcclResult GetNotifyNum(uint32_t* notifyNum) const;
    HcclResult GetRemoteMems(uint32_t* memNum, CommMem** remoteMem, char*** memInfos);
    HcclResult UpdateMemInfo(HcommMemHandle* memHandles, uint32_t memHandleNum) override;
    HcclResult Clean();
    HcclResult Resume();
    HcclResult ChannelFence();
    HcclResult notify_record(uint32_t remote_notify_idx);
    HcclResult notify_wait(uint32_t local_notify_idx, uint32_t timeout);
    HcclResult WriteWithNotify(void* dst_, const void* src_, uint64_t len_, uint32_t remote_notify_idx);
    HcclResult Write(void* dst_, const void* src_, uint64_t len_);
    HcclResult Read(void* dst_, const void* src_, uint64_t len_);
    uint32_t get_die_id() const;
    uint32_t get_channel_id() const;
    HcclResult get_rmt_cke_by_index(uint32_t index, uint32_t& rmt_cke_id_) const;
    HcclResult get_rmt_xn_by_index(uint32_t index, uint32_t& rmt_xn_id_) const;
    HcclResult get_loc_cke_by_index(uint32_t index, uint32_t& loc_cke_id) const;
    HcclResult get_loc_xn_by_index(uint32_t index, uint32_t& loc_xn_id) const;
    HcclResult get_rmt_buffer(uint64_t& addr_, uint32_t& size, uint32_t& token_id, uint32_t& token_value) const;
};
} // namespace asc

#endif
