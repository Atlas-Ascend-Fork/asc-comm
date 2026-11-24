/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_PROFILING_INTERNAL_H
#define ASCCOMM_PROFILING_INTERNAL_H
#include <cstdint>
struct AsccommProfOp {
    uint32_t size, rank, rankSize, dataType, reserved;
    uint64_t count;
    const char* comm;
    const char* op;
    // Optional selected executor name for L0 reporting.
    const char* algorithm;
};
extern "C" {
int asccomm_prof_begin(const AsccommProfOp*);
// L1 currently decodes UB tasks only. Binding another protocol disables L1
// for this operation, preserves L0 reporting, and never rejects communication.
// channelHandle is the address of a live ChannelEntity owned by the caller.
int asccomm_prof_bind_channel(uint64_t channelHandle, uint32_t peer);
int asccomm_prof_end(int executionStatus);
}
#endif
