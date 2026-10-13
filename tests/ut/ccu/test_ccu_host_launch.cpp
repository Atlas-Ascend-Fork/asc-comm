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
#include "stub/ccu_launch_stub.h"

namespace {
constexpr const char* DUMMY_KERNEL_NAME = "DummyKernel";
uint32_t g_dummyKernelCallCount = 0;
const void* g_dummyKernelObservedArg = nullptr;

void DummyKernel(void* arg)
{
    ++g_dummyKernelCallCount;
    g_dummyKernelObservedArg = arg;
}

asccomm_launch_kernel_cfg MakeLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    cfg.ccu_ins = CcuInsHandle{};
    cfg.stream = reinterpret_cast<aclrtStream>(0x22);
    cfg.attrs = nullptr;
    return cfg;
}
} // namespace

class TestCcuHostLaunch : public testing::Test {
protected:
    void SetUp() override
    {
        ResetCcuLaunchStub();
        SetCcuLaunchStubTaskArgsNum(3U);
        g_dummyKernelCallCount = 0;
        g_dummyKernelObservedArg = nullptr;
    }
};

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

TEST_F(TestCcuHostLaunch, LaunchSuccessForwardsRegisterAndLaunchParameters)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0x5101U;
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 1U);
    EXPECT_EQ(observed.calls.registerKernel, 1U);
    EXPECT_EQ(observed.calls.registerEnd, 1U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 1U);
    EXPECT_EQ(observed.calls.kernelLaunch, 1U);
    EXPECT_EQ(observed.lastInsHandle.ccuInsKey, cfg.ccu_ins.ccuInsKey);
    EXPECT_EQ(observed.lastInsHandle.ccuInsPtr, cfg.ccu_ins.ccuInsPtr);
    EXPECT_EQ(observed.lastDieId, 0U);
    EXPECT_EQ(observed.lastKernelName, DUMMY_KERNEL_NAME);
    EXPECT_NE(observed.lastKernelHandle, 0U);
    EXPECT_EQ(observed.lastTaskArgsNum, 3U);
    EXPECT_EQ(observed.lastStream, cfg.stream);
    EXPECT_EQ(observed.lastTaskArgs, args);
    EXPECT_EQ(observed.lastLaunchArgNum, 3U);
    EXPECT_EQ(g_dummyKernelCallCount, 1U);
    EXPECT_EQ(g_dummyKernelObservedArg, args);
}

TEST_F(TestCcuHostLaunch, NullKernelNameIsForwardedToRegister)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), nullptr, &cfg, args), CCU_SUCCESS);

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.lastKernelName, nullptr);
    EXPECT_EQ(observed.calls.kernelLaunch, 1U);
}

TEST_F(TestCcuHostLaunch, RegisterFailureClosesRegisterRound)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    SetCcuLaunchStubResults(
        CcuResult::CCU_SUCCESS, CcuResult::CCU_E_RUNTIME, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS,
        CcuResult::CCU_SUCCESS);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_RUNTIME);

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 1U);
    EXPECT_EQ(observed.calls.registerKernel, 1U);
    EXPECT_EQ(observed.calls.registerEnd, 1U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 0U);
    EXPECT_EQ(observed.calls.kernelLaunch, 0U);
}

TEST_F(TestCcuHostLaunch, LaunchFailureIsPropagatedAndCachedHandleIsReused)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0x5301U;
    uint64_t args[] = {1, 2, 3};
    SetCcuLaunchStubResults(
        CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS,
        CcuResult::CCU_E_RUNTIME);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_RUNTIME);

    SetCcuLaunchStubResults(
        CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS,
        CcuResult::CCU_SUCCESS);
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 1U);
    EXPECT_EQ(observed.calls.registerKernel, 1U);
    EXPECT_EQ(observed.calls.registerEnd, 1U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 2U);
    EXPECT_EQ(observed.calls.kernelLaunch, 2U);
}

TEST_F(TestCcuHostLaunch, SameCacheTagRegistersOnceAndLaunchesEachTime)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0x5401U;
    uint64_t args[] = {1, 2, 3};

    for (uint32_t idx = 0; idx < 2; idx++) {
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 1U);
    EXPECT_EQ(observed.calls.registerKernel, 1U);
    EXPECT_EQ(observed.calls.registerEnd, 1U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 2U);
    EXPECT_EQ(observed.calls.kernelLaunch, 2U);
}

TEST_F(TestCcuHostLaunch, ZeroCacheTagRegistersEachLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0U;
    uint64_t args[] = {1, 2, 3};

    for (uint32_t idx = 0; idx < 2; idx++) {
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 2U);
    EXPECT_EQ(observed.calls.registerKernel, 2U);
    EXPECT_EQ(observed.calls.registerEnd, 2U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 2U);
    EXPECT_EQ(observed.calls.kernelLaunch, 2U);
}

TEST_F(TestCcuHostLaunch, DifferentCacheTagsRegisterIndependently)
{
    uint64_t args[] = {1, 2, 3};
    for (uint64_t cacheTag = 0x5501U; cacheTag <= 0x5502U; cacheTag++) {
        asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
        cfg.ccu_schd.binary_cache_tag = cacheTag;
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerStart, 2U);
    EXPECT_EQ(observed.calls.registerKernel, 2U);
    EXPECT_EQ(observed.calls.registerEnd, 2U);
    EXPECT_EQ(observed.calls.getTaskArgsNum, 2U);
    EXPECT_EQ(observed.calls.kernelLaunch, 2U);
}

TEST_F(TestCcuHostLaunch, HcompatCfgForwardsThroughNewLaunchChain)
{
    HcommLaunchKernelCfg hcompatCfg{};
    hcompatCfg.ccuSchd.numBlocks = 1;
    hcompatCfg.ccuSchd.reserved = 0;
    hcompatCfg.ccuSchd.phyDieMask = 0x02;
    hcompatCfg.ccuSchd.binaryCacheTag = 0x5601U;
    hcompatCfg.ccuIns = CcuInsHandle{};
    hcompatCfg.stream = reinterpret_cast<aclrtStream>(0x33);
    hcompatCfg.attrs = nullptr;
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &hcompatCfg, args),
        CCU_SUCCESS);

    const CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.lastDieId, 1U);
    EXPECT_EQ(observed.lastStream, hcompatCfg.stream);
    EXPECT_EQ(observed.lastTaskArgs, args);
    EXPECT_EQ(observed.lastLaunchArgNum, 3U);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
