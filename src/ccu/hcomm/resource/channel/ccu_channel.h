/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_CHANNEL_H
#define ASCCOMM_CCU_CHANNEL_H

#include <cstdint>

#include "hccl/hccl_types.h"
#include "hcomm/hcomm_ccu_channel_api.h"

namespace hcomm {

class CcuChannel {
public:
    explicit CcuChannel(ChannelHandle channel);

    CcuChannel *operator->();
    const CcuChannel *operator->() const;
    HcclResult GetResult() const;
    uint32_t GetDieId() const;
    uint32_t GetChannelId() const;
    HcclResult GetLocCkeByIndex(uint32_t index, uint32_t &localCkeId) const;
    HcclResult GetLocXnByIndex(uint32_t index, uint32_t &localXnId) const;
    HcclResult GetRmtCkeByIndex(uint32_t index, uint32_t &remoteCkeId) const;
    HcclResult GetRmtXnByIndex(uint32_t index, uint32_t &remoteXnId) const;
    HcclResult GetRmtBuffer(uint64_t &addr, uint32_t &size, uint32_t &tokenId, uint32_t &tokenValue) const;

private:
    HcclResult Init(ChannelHandle channel);
    HcclResult GetIdByRange(const HcommCcuIdRangePod &range, uint32_t index, uint32_t &id) const;

    HcommCcuChannelPod channelPod_{};
    HcclResult result_{HCCL_E_INTERNAL};
};

} // namespace hcomm

#endif // ASCCOMM_CCU_CHANNEL_H
