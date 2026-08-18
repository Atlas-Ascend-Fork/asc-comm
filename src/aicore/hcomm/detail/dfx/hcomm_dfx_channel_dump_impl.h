/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DETAIL_DFX_HCOMM_DFX_CHANNEL_DUMP_IMPL_H
#define ASCCOMM_SRC_AICORE_HCOMM_DETAIL_DFX_HCOMM_DFX_CHANNEL_DUMP_IMPL_H

#include "hcomm/dfx/hcomm_channel_dump_info.h"

#include <cstdint>

namespace AscendC {

constexpr uint32_t HCOMM_DFX_QUEUE_READ_SUCCESS = 0U;
constexpr uint32_t HCOMM_DFX_QUEUE_READ_INVALID_PARAM = 1U;

struct HcommDfxQueueReadArgs {
    uint64_t srcAddr;
    uint64_t resultAddr;
    uint32_t size;
    uint32_t reserved;
};

struct HcommDfxQueueReadResult {
    uint32_t ret;
    uint32_t size;
    uint8_t data[HCOMM_DFX_QUEUE_ENTRY_BYTES];
};

namespace HcommDfx {

void Trace(const char *format, ...);
void *CreateQueueReader();
void DestroyQueueReader(void *reader);
bool ReadQueueEntry(void *reader, uint64_t addr, uint8_t *data, uint32_t size);

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DETAIL_DFX_HCOMM_DFX_CHANNEL_DUMP_IMPL_H
