/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef SAMPLE_ALLGATHER_DEF_H
#define SAMPLE_ALLGATHER_DEF_H

#include <cstdint>

constexpr uint32_t HCOMM_DFX_NRANKS = 2U;
constexpr uint64_t HCOMM_DFX_COMM_BUF_SIZE = 268439552ULL;
constexpr uint64_t SAMPLE_ALLGATHER_NORMAL_LEN = 256ULL;
constexpr uint32_t HCOMM_DFX_WORKSPACE_SIZE = 512U;

struct SampleAllGatherContext {
    uint64_t channelHandle;
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;
    uint64_t len;
    int32_t writeRet;
    int32_t drainRet;
    uint32_t rankId;
    uint32_t reserved;
};

#endif // SAMPLE_ALLGATHER_DEF_H
