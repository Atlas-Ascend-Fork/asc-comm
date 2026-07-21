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

#include "ccu_provider_internal.h"

namespace hcomm {

class CcuChannel {
public:
    explicit CcuChannel(AscCommCcuChannelHandle handle) : handle_(handle) {}

    HcclResult GetDieId(uint32_t &value) const { return Query(0, &CcuChannelData::dieId, value); }
    HcclResult GetChannelId(uint32_t &value) const { return Query(0, &CcuChannelData::channelId, value); }
    uint32_t GetDieId() const
    {
        uint32_t value = 0;
        (void)GetDieId(value);
        return value;
    }
    uint32_t GetChannelId() const
    {
        uint32_t value = 0;
        (void)GetChannelId(value);
        return value;
    }
    HcclResult GetLocCkeByIndex(uint32_t index, uint32_t &value) const
    {
        return Query(index, &CcuChannelData::localCkeId, value);
    }
    HcclResult GetLocXnByIndex(uint32_t index, uint32_t &value) const
    {
        return Query(index, &CcuChannelData::localXnId, value);
    }
    HcclResult GetRmtCkeByIndex(uint32_t index, uint32_t &value) const
    {
        return Query(index, &CcuChannelData::remoteCkeId, value);
    }
    HcclResult GetRmtXnByIndex(uint32_t index, uint32_t &value) const
    {
        return Query(index, &CcuChannelData::remoteXnId, value);
    }
    HcclResult GetRmtBuffer(uint64_t &address, uint32_t &size, uint32_t &tokenId,
        uint32_t &tokenValue) const
    {
        CcuChannelData data{};
        const CcuResult result = CcuQueryChannel(handle_, 0, data);
        address = data.remoteAddress;
        size = data.remoteSize;
        tokenId = data.remoteTokenId;
        tokenValue = data.remoteTokenValue;
        return static_cast<HcclResult>(result);
    }

private:
    HcclResult Query(uint32_t index, uint32_t CcuChannelData::*member, uint32_t &value) const
    {
        CcuChannelData data{};
        const CcuResult result = CcuQueryChannel(handle_, index, data);
        value = data.*member;
        return static_cast<HcclResult>(result);
    }

    AscCommCcuChannelHandle handle_{0};
};

} // namespace hcomm

#endif
