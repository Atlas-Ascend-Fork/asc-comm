/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int32_t rtGetTaskIdAndStreamID(uint32_t* taskId, uint32_t* streamId)
{
    if (taskId == nullptr || streamId == nullptr)
        return 1;
    const char* mode = std::getenv("CCU_FAKE_TASK_QUERY_MODE");
    if (mode != nullptr && std::strcmp(mode, "fail") == 0)
        return 1;
    *taskId = 777;
    *streamId = 888;
    return 0;
}
