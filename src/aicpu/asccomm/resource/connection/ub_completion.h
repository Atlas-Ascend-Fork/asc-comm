/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_UB_COMPLETION_H
#define ASCCOMM_UB_COMPLETION_H
#include <cstdint>
struct AsccommUbCompletion {
    uint64_t generation = 0;
    bool done = false, failed = false, submitted = false;
};
// Non-owning worker-local tag. The owner must outlive every outstanding slot.
// Generation distinguishes submissions on the same live completion.
struct AsccommUbCompletionTag {
    AsccommUbCompletion* completion = nullptr;
    uint64_t generation = 0;
    bool Current() const { return completion && generation == completion->generation; }
    void Complete() const
    {
        if (Current()) {
            completion->done = true;
        }
    }
    void Fail() const
    {
        if (Current()) {
            completion->failed = true;
        }
    }
};
#endif
