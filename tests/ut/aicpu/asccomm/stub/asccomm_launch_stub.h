/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_LAUNCH_STUB_H
#define ASCCOMM_LAUNCH_STUB_H

#include <cstdint>
#include <vector>

#include "asccomm_launch.h"

struct LaunchStubState {
    std::vector<ThreadHandle> lastThreads;
    std::vector<ThreadHandle> lastTryThreads;
    AsccommResult taskResult{ASCCOMM_SUCCESS};
    AsccommResult tryResult{ASCCOMM_SUCCESS};
};

extern LaunchStubState g_launchStubState;

#endif // ASCCOMM_LAUNCH_STUB_H
