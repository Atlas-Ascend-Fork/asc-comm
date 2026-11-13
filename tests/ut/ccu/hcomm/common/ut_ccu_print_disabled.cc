/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#define ASCENDC_DUMP 0
#include "ccu/hcomm/ccu_print.hpp"
#include <gtest/gtest.h>

TEST(CcuPrintDisabledTest, SkipsArgumentsAndReturnsSuccess)
{
    int calls = 0;
    const auto format = [&]() -> const char* {
        ++calls;
        return "%u";
    };
    (void)format;
    EXPECT_EQ(CCU_PRINTF(format(), ++calls), CCU_SUCCESS);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(CCU_PRINTF(nullptr), CCU_SUCCESS);
    EXPECT_EQ(CCU_PRINTF("%s"), CCU_SUCCESS);
}
