/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
#define ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H

#include "hcomm/hcomm_common.h"

#include <cstdint>

namespace AscendC {

// 算子接入 HCOMM 通道维测能力的两个公开接口。
//
// HcommChannelExceptionCallbackRegister：launch 前调用一次，为目标 kernel 绑定异常回调。
//   绑定后，一旦 kernel 触发 AIC Error，框架会在回调里轻量记录现场（仅置 pending，不做
//   任何 D2H / 打印 / launch，避免在异常态挂死）。不注册则异常发生时不记录，后续 dump 静默返回。
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol);

// HcommChannelInfoDump：aclrtSynchronizeStream 返回错误后，在 host 安全点调用。
//   若存在已记录的异常，则采集并打印该 channel 的 Channel / SQ / CQ / WQE / CQE 现场；
//   无异常则直接返回成功，可无条件调用。
int32_t HcommChannelInfoDump(ChannelHandle channel);

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
