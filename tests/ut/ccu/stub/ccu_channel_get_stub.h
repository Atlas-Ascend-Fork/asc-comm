/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_CHANNEL_GET_STUB_H
#define ASCCOMM_CCU_CHANNEL_GET_STUB_H

#include "hcomm/hcomm_types.h"
#include "hcomm/hcomm_ccu_resource.h"

namespace asc {

// ascCustom 槽 0（getChannelEntity）的 stub：预置快照 + 可注入的失败返回
void SetHcommCcuChannelGetEntityStub(const HcommCcuChannelEntity& channelEntity);
void SetHcommCcuChannelGetEntityStubResult(HcommResult result);
void ResetHcommCcuChannelGetEntityStub();
} // namespace asc

#endif
