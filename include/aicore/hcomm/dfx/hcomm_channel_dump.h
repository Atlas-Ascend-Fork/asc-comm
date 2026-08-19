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

#include "hcomm_channel_dump_info.h"
#include "hcomm/hcomm_common.h"

#include <cstdint>

namespace AscendC {

int32_t HcommDfxGetChannelDumpInfoSize(uint32_t channelNum, const ChannelHandle *channels, uint32_t *len);
int32_t HcommDfxDumpChannelInfo(uint32_t channelNum, const ChannelHandle *channels, void *buffer, uint32_t len);
int32_t HcommDfxBindKernelException(const void *kernelSymbol, void *deviceContext);
bool HcommDfxHasPendingException();
void HcommDfxClearPendingException();
int32_t HcommDfxDumpAndPrintChannelIfException(ChannelHandle channel);

} // namespace AscendC

#endif // ASCCOMM_INCLUDE_AICORE_HCOMM_DFX_HCOMM_CHANNEL_DUMP_H
