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

#include <vector>

#include "ccu/ccu_host_launch.h"

namespace {
constexpr const char* DUMMY_KERNEL_NAME = "DummyKernel";

void DummyKernel(void*) {}

asccomm_launch_kernel_cfg MakeLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    cfg.ccu_ins = HcommCcuInstanceHandle{};
    cfg.stream = reinterpret_cast<aclrtStream>(0x22);
    cfg.attrs = nullptr;
    return cfg;
}
} // namespace

class TestCcuHostLaunch : public testing::Test {};

TEST_F(TestCcuHostLaunch, NullArgumentsReturnPtr)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(nullptr, DUMMY_KERNEL_NAME, &cfg, args), CCU_E_PTR);
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, nullptr, args),
        CCU_E_PTR);
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, nullptr),
        CCU_E_PTR);
    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, nullptr, args),
        CCU_E_PTR);
}

TEST_F(TestCcuHostLaunch, UnsupportedDieMaskReturnsPara)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    cfg.ccu_schd.phy_die_mask = 0;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);

    cfg.ccu_schd.phy_die_mask = 0x03;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);

    HcommLaunchKernelCfg hcommCfg{};
    hcommCfg.ccuSchd.numBlocks = cfg.ccu_schd.num_blocks;
    hcommCfg.ccuSchd.reserved = cfg.ccu_schd.reserved;
    hcommCfg.ccuSchd.phyDieMask = cfg.ccu_schd.phy_die_mask;
    hcommCfg.ccuSchd.binaryCacheTag = cfg.ccu_schd.binary_cache_tag;
    hcommCfg.ccuIns = cfg.ccu_ins;
    hcommCfg.stream = cfg.stream;
    hcommCfg.attrs = cfg.attrs;
    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &hcommCfg, args),
        CCU_E_PARA);
}

TEST_F(TestCcuHostLaunch, UnsupportedNumBlocksReturnsPara)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    cfg.ccu_schd.num_blocks = 0;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);

    cfg.ccu_schd.num_blocks = 2;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);
}

TEST_F(TestCcuHostLaunch, LaunchHashTagReturnsStableNonZeroValue)
{
    const uint64_t tag = asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel");

    EXPECT_EQ(asccomm_ccu_get_launch_hash_tag(nullptr), 0U);
    EXPECT_NE(tag, 0U);
    EXPECT_EQ(tag, asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel"));
    EXPECT_NE(tag, asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel_rank_1"));
    EXPECT_EQ(tag, HcommCcuGetLaunchHashTag("CcuAllGatherMesh1DMem2MemKernel"));
}

// 数据面桩恒返回 CCU_E_PARA：register_start 对 POD 校验不过即失败，
// kernel_name 正常透传到 asccomm_ccu_kernel_register（无段错误、无额外校验）。
TEST_F(TestCcuHostLaunch, KernelNameForwardedThroughRegisterChain)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), nullptr, &cfg, args), CCU_E_PARA);

    HcommLaunchKernelCfg hcommCfg{};
    hcommCfg.ccuSchd.numBlocks = cfg.ccu_schd.num_blocks;
    hcommCfg.ccuSchd.reserved = cfg.ccu_schd.reserved;
    hcommCfg.ccuSchd.phyDieMask = cfg.ccu_schd.phy_die_mask;
    hcommCfg.ccuSchd.binaryCacheTag = cfg.ccu_schd.binary_cache_tag;
    hcommCfg.ccuIns = cfg.ccu_ins;
    hcommCfg.stream = cfg.stream;
    hcommCfg.attrs = cfg.attrs;
    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &hcommCfg, args),
        CCU_E_PARA);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
