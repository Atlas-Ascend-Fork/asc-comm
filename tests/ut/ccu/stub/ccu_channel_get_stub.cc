/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_channel_get_stub.h"

#include "hcomm/common/ccu_device_context.h"
#include "hcomm/resource/common/asc_ccu_res_snapshot.h"

// 本文件底部提供的 stub 入口（控制操作槽 0：填充实体快照）
extern "C" int32_t HcommCcuGetChannelEntity(ChannelHandle channel, HcommCcuChannelEntity* channelEntity);

namespace asc {
namespace {

HcommCcuChannelEntity channelEntityStub{};
HcommResult queryResultStub = HCCL_SUCCESS;
bool channelEntityStubConfigured = false;

} // namespace

void SetHcommCcuChannelGetEntityStub(const HcommCcuChannelEntity& channelEntity)
{
    channelEntityStub = channelEntity;
    queryResultStub = HCCL_SUCCESS;
    channelEntityStubConfigured = true;

    // 仅注入本 stub 关心的槽 0（getChannelEntity）
    uint64_t ops[8]{};
    ops[HCOMM_CCU_CONTROL_SLOT_CHANNEL_ENTITY] = reinterpret_cast<uint64_t>(HcommCcuGetChannelEntity);
    asc::set_current_ccu_control_ops(ops);
}

void SetHcommCcuChannelGetEntityStubResult(HcommResult result) { queryResultStub = result; }

void ResetHcommCcuChannelGetEntityStub()
{
    channelEntityStub = {};
    queryResultStub = HCCL_SUCCESS;
    channelEntityStubConfigured = false;
    asc::set_current_ccu_control_ops({});
}

} // namespace asc

// 填充预置快照
int32_t HcommCcuGetChannelEntity(ChannelHandle, HcommCcuChannelEntity* channelEntity)
{
    if (channelEntity == nullptr || !asc::channelEntityStubConfigured) {
        return static_cast<int32_t>(HCCL_E_PTR);
    }
    if (asc::queryResultStub != HCCL_SUCCESS) {
        return static_cast<int32_t>(asc::queryResultStub);
    }

    *channelEntity = asc::channelEntityStub;
    return static_cast<int32_t>(HCCL_SUCCESS);
}
