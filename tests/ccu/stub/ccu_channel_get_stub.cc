/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_channel_get_stub.h"

#include "hcomm_c_adpt.h"

namespace hcomm {
Channel *channelStub = nullptr;

void SetHcommChannelGetStub(Channel *channel)
{
    channelStub = channel;
}

void ResetHcommChannelGetStub()
{
    channelStub = nullptr;
}

} // namespace hcomm

HcommResult HcommChannelGet(ChannelHandle channelHandle, void **channel)
{
    if (channel == nullptr || hcomm::channelStub == nullptr) {
        return HCCL_E_PTR;
    }
    *channel = hcomm::channelStub;
    return HCCL_SUCCESS;
}
