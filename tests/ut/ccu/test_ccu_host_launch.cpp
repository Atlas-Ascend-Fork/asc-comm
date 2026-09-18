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

#include "acl/acl_rt.h"
#include "ccu/ccu_host_launch.h"

namespace {
uint32_t g_registerStartCallCount = 0;
CcuKernelHandle g_nextKernelHandle = 1;
uint32_t g_threadAllocCallCount = 0;
uint32_t g_threadFreeCallCount = 0;
uint32_t g_kernelLaunchCallCount = 0;
uintptr_t g_nextStream = 0x22;
CommEngine g_observedThreadEngine = COMM_ENGINE_RESERVED;
aclrtStream g_observedThreadStream = nullptr;
ThreadHandle g_nextThreadHandle = 0x100;
ThreadHandle g_observedLaunchThread = 0;
CcuKernelHandle g_observedLaunchKernel = 0;
const void* g_observedLaunchArgs = nullptr;
uint32_t g_observedLaunchArgNum = 0;
uint32_t g_taskArgsNum = 3;
CcuResult g_kernelLaunchResult = CCU_SUCCESS;
HcommResult g_threadAllocResult = HCCL_SUCCESS;
HcommResult g_threadFreeResult = HCCL_SUCCESS;

void DummyKernel(void*) {}

asccomm_launch_kernel_cfg MakeLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    cfg.ccu_ins = 0x11;
    cfg.stream = reinterpret_cast<aclrtStream>(g_nextStream++);
    cfg.attrs = nullptr;
    return cfg;
}
} // namespace

extern "C" CcuResult HcommCcuKernelRegisterStart(CcuInsHandle)
{
    ++g_registerStartCallCount;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegister(
    CcuInsHandle, uint32_t, const char*, const void*, const void**, uint32_t, CcuKernelHandle* kernelHandle)
{
    if (kernelHandle == nullptr) {
        return CCU_E_PTR;
    }
    *kernelHandle = g_nextKernelHandle++;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle) { return CCU_SUCCESS; }

extern "C" CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle, uint32_t* taskArgsNum)
{
    if (taskArgsNum == nullptr) {
        return CCU_E_PTR;
    }
    *taskArgsNum = g_taskArgsNum;
    return CCU_SUCCESS;
}

extern "C" HcommResult HcommThreadAllocWithStream(CommEngine engine, aclrtStream stream, uint32_t, ThreadHandle* thread)
{
    ++g_threadAllocCallCount;
    g_observedThreadEngine = engine;
    g_observedThreadStream = stream;
    if (g_threadAllocResult == HCCL_SUCCESS && thread != nullptr) {
        *thread = g_nextThreadHandle++;
    }
    return g_threadAllocResult;
}

extern "C" HcommResult HcommThreadFree(const ThreadHandle*, uint32_t)
{
    ++g_threadFreeCallCount;
    return g_threadFreeResult;
}

extern "C" CcuResult HcommCcuKernelLaunch(
    ThreadHandle thread, CcuKernelHandle kernel, const void* launchArgs, uint32_t argNum)
{
    ++g_kernelLaunchCallCount;
    g_observedLaunchThread = thread;
    g_observedLaunchKernel = kernel;
    g_observedLaunchArgs = launchArgs;
    g_observedLaunchArgNum = argNum;
    return g_kernelLaunchResult;
}

class TestCcuHostLaunch : public testing::Test {
protected:
    void SetUp() override
    {
        g_registerStartCallCount = 0;
        g_threadAllocCallCount = 0;
        g_threadFreeCallCount = 0;
        g_kernelLaunchCallCount = 0;
        g_observedThreadEngine = COMM_ENGINE_RESERVED;
        g_observedThreadStream = nullptr;
        g_observedLaunchThread = 0;
        g_observedLaunchKernel = 0;
        g_observedLaunchArgs = nullptr;
        g_observedLaunchArgNum = 0;
        g_taskArgsNum = 3;
        g_kernelLaunchResult = CCU_SUCCESS;
        g_threadAllocResult = HCCL_SUCCESS;
        g_threadFreeResult = HCCL_SUCCESS;
    }
};

TEST_F(TestCcuHostLaunch, NullArgumentsReturnPtr)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(nullptr, &cfg, args), CCU_E_PTR);
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), nullptr, args), CCU_E_PTR);
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, nullptr), CCU_E_PTR);
    EXPECT_EQ(HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), nullptr, args), CCU_E_PTR);
}

TEST_F(TestCcuHostLaunch, UnsupportedDieMaskReturnsPara)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    cfg.ccu_schd.phy_die_mask = 0;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_PARA);

    cfg.ccu_schd.phy_die_mask = 0x03;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_PARA);

    HcommLaunchKernelCfg hcommCfg{};
    hcommCfg.ccuSchd.numBlocks = cfg.ccu_schd.num_blocks;
    hcommCfg.ccuSchd.reserved = cfg.ccu_schd.reserved;
    hcommCfg.ccuSchd.phyDieMask = cfg.ccu_schd.phy_die_mask;
    hcommCfg.ccuSchd.binaryCacheTag = cfg.ccu_schd.binary_cache_tag;
    hcommCfg.ccuIns = cfg.ccu_ins;
    hcommCfg.stream = cfg.stream;
    hcommCfg.attrs = cfg.attrs;
    EXPECT_EQ(HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), &hcommCfg, args), CCU_E_PARA);
}

TEST_F(TestCcuHostLaunch, UnsupportedNumBlocksReturnsPara)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    cfg.ccu_schd.num_blocks = 0;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_PARA);

    cfg.ccu_schd.num_blocks = 2;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_PARA);
}

TEST_F(TestCcuHostLaunch, LaunchUsesAllocatedThreadAndHcommKernelLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_threadAllocCallCount, 1U);
    EXPECT_EQ(g_observedThreadEngine, COMM_ENGINE_CCU);
    EXPECT_EQ(g_observedThreadStream, cfg.stream);
    EXPECT_EQ(g_kernelLaunchCallCount, 1U);
    EXPECT_NE(g_observedLaunchThread, 0U);
    EXPECT_NE(g_observedLaunchKernel, 0U);
    EXPECT_EQ(g_observedLaunchArgs, args);
    EXPECT_EQ(g_observedLaunchArgNum, 3U);
    EXPECT_EQ(g_threadFreeCallCount, 0U);
}

TEST_F(TestCcuHostLaunch, TooManyTaskArgsReturnsNotSupportBeforeThreadAllocation)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1};
    g_taskArgsNum = 14;

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_NOT_SUPPORT);
    EXPECT_EQ(g_threadAllocCallCount, 0U);
    EXPECT_EQ(g_kernelLaunchCallCount, 0U);
}

TEST_F(TestCcuHostLaunch, ThreadAllocationFailureStopsLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_threadAllocResult = HCCL_E_UNAVAIL;

    // MakeLaunchCfg uses a fresh stream, so this test does not reuse a cached handle.
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_UNAVAIL);
    EXPECT_EQ(g_kernelLaunchCallCount, 0U);
    EXPECT_EQ(g_threadFreeCallCount, 0U);
}

TEST_F(TestCcuHostLaunch, KernelLaunchFailureKeepsThreadAlive)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_kernelLaunchResult = CCU_E_RUNTIME;

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_RUNTIME);
    EXPECT_EQ(g_kernelLaunchCallCount, 1U);
    EXPECT_EQ(g_threadFreeCallCount, 0U);
}

TEST_F(TestCcuHostLaunch, RepeatedLaunchesAllocateAndFreeIndependentThreads)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_threadAllocCallCount, 1U);
    EXPECT_EQ(g_kernelLaunchCallCount, 2U);
    EXPECT_EQ(g_threadFreeCallCount, 0U);
}

TEST_F(TestCcuHostLaunch, DifferentStreamsUseIndependentThreads)
{
    asccomm_launch_kernel_cfg firstCfg = MakeLaunchCfg();
    asccomm_launch_kernel_cfg secondCfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &firstCfg, args), CCU_SUCCESS);
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &secondCfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_threadAllocCallCount, 2U);
    EXPECT_EQ(g_kernelLaunchCallCount, 2U);
    EXPECT_EQ(g_threadFreeCallCount, 0U);
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

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
