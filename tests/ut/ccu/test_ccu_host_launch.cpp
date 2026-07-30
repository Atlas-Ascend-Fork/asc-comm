/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include "ccu/ccu_host_launch.h"

namespace {
void DummyKernel(void*) {}

asccomm_launch_kernel_cfg MakeLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    cfg.ccu_ins = 0x11;
    cfg.stream = reinterpret_cast<aclrtStream>(0x22);
    cfg.attrs = nullptr;
    return cfg;
}
} // namespace

class AsccommCcuHostLaunchTest : public testing::Test {};

TEST_F(AsccommCcuHostLaunchTest, NullArgumentsReturnPtr)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(nullptr, &cfg, args), CCU_E_PTR);
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), nullptr, args), CCU_E_PTR);
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, nullptr), CCU_E_PTR);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
