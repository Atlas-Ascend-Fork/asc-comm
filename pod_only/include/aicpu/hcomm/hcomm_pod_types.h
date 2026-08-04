/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_TYPES_H
#define HCOMM_POD_TYPES_H

#include <stdint.h>

#ifndef CHANNEL_HANDLE_DEFINED
#define CHANNEL_HANDLE_DEFINED
typedef uint64_t ChannelHandle;
#endif

#ifndef THREAD_HANDLE_DEFINED
#define THREAD_HANDLE_DEFINED
typedef uint64_t ThreadHandle;
#endif

#ifndef HCOMM_COMM_ABI_HEADER_DEFINED
#define HCOMM_COMM_ABI_HEADER_DEFINED
typedef struct {
    uint32_t version;
    uint32_t magicWord;
    uint32_t size;
    uint32_t reserved;
} CommAbiHeader;
#endif

#ifndef HCOMM_COMM_ENGINE_DEFINED
#define HCOMM_COMM_ENGINE_DEFINED
typedef enum {
    COMM_ENGINE_RESERVED = -1,
    COMM_ENGINE_CPU = 0,
    COMM_ENGINE_CPU_TS = 1,
    COMM_ENGINE_AICPU = 2,
    COMM_ENGINE_AICPU_TS = 3,
    COMM_ENGINE_AIV = 4,
    COMM_ENGINE_CCU = 5,
} CommEngine;
#endif

#ifndef HCOMM_COMM_PROTOCOL_DEFINED
#define HCOMM_COMM_PROTOCOL_DEFINED
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,
    COMM_PROTOCOL_HCCS = 0,
    COMM_PROTOCOL_ROCE = 1,
    COMM_PROTOCOL_PCIE = 2,
    COMM_PROTOCOL_SIO = 3,
    COMM_PROTOCOL_UBC_CTP = 4,
    COMM_PROTOCOL_UBC_TP = 5,
    COMM_PROTOCOL_UB_MEM = 6,
    COMM_PROTOCOL_UBOE = 7,
    COMM_PROTOCOL_HCCS_ONLY = 8,
    COMM_PROTOCOL_UBG = 9,
} CommProtocol;
#endif

#endif // HCOMM_POD_TYPES_H
