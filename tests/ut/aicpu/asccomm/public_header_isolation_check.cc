/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License");
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <stdint.h>

#include "asccomm/asccomm_primitives.h"

static_assert(sizeof(ThreadHandle) == sizeof(uint64_t), "ThreadHandle ABI changed");
static_assert(sizeof(ChannelHandle) == sizeof(uint64_t), "ChannelHandle ABI changed");

#ifdef HCOMM_RES_ENTITY_DEFS_H
#error "The public AICPU data-plane header must not expose hcomm resource entity definitions"
#endif

int main() { return 0; }
