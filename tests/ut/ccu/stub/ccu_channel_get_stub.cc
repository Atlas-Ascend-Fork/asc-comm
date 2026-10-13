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

// 本文件底部提供的 stub 入口（ascCustom 槽 0：返回实体借用地址）
extern "C" int32_t HcommCcuGetChannelEntity(ChannelHandle channel, uint64_t* channelEntityPtr);

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
    uint64_t ops[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]{};
    ops[HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY] = reinterpret_cast<uint64_t>(HcommCcuGetChannelEntity);
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

namespace asc {
namespace {
HcommCcuChannelEntity channelEntityStorage{}; // stub 的"实体"存储：借出该地址
}
} // namespace asc

// 借用地址指向预置快照
int32_t HcommCcuGetChannelEntity(ChannelHandle, uint64_t* channelEntityPtr)
{
    if (channelEntityPtr == nullptr || !asc::channelEntityStubConfigured) {
        return static_cast<int32_t>(HCCL_E_PTR);
    }
    if (asc::queryResultStub != HCCL_SUCCESS) {
        return static_cast<int32_t>(asc::queryResultStub);
    }

    asc::channelEntityStorage = asc::channelEntityStub;
    *channelEntityPtr = reinterpret_cast<uint64_t>(&asc::channelEntityStorage);
    return static_cast<int32_t>(HCCL_SUCCESS);
}
