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
#include "hcomm/hcomm_res_defs.h" // ThreadHandle/CommEngine/HcommResult（hcomm 旧接口桩签名）
#include "stub/ccu_launch_stub.h"

namespace {
constexpr const char* DUMMY_KERNEL_NAME = "DummyKernel";
uint32_t g_dummyKernelCallCount = 0;
const void* g_dummyKernelObservedArg = nullptr;
HcommCcuInstanceEntity g_defaultCcuInstance{};

// hcomm 旧接口路径桩观测（测试文件内强定义覆盖实现中的 weak 符号）
uint32_t g_hcommRegisterStartCalls = 0;
uint32_t g_hcommRegisterCalls = 0;
uint32_t g_hcommRegisterEndCalls = 0;
uint32_t g_hcommGetTaskArgsNumCalls = 0;
uint32_t g_hcommKernelLaunchCalls = 0;
uint32_t g_hcommThreadAllocCalls = 0;
CcuInsHandle g_hcommObservedIns = 0;
uint32_t g_hcommObservedDieId = 0;
CcuKernelHandle g_hcommObservedKernelHandle = 0;
const void* g_hcommObservedTaskArgs = nullptr;
uint32_t g_hcommObservedArgNum = 0;
ThreadHandle g_hcommObservedThread = 0;
ThreadHandle g_hcommObservedAllocThread = 0;
uint32_t g_hcommTaskArgsNum = 3;
CcuResult g_hcommKernelLaunchResult = CCU_SUCCESS;
HcommResult g_hcommThreadAllocResult = 0;
CcuKernelHandle g_hcommNextKernelHandle = 1;

void ResetHcommStub()
{
    g_hcommRegisterStartCalls = 0;
    g_hcommRegisterCalls = 0;
    g_hcommRegisterEndCalls = 0;
    g_hcommGetTaskArgsNumCalls = 0;
    g_hcommKernelLaunchCalls = 0;
    g_hcommThreadAllocCalls = 0;
    g_hcommObservedIns = 0;
    g_hcommObservedDieId = 0;
    g_hcommObservedKernelHandle = 0;
    g_hcommObservedTaskArgs = nullptr;
    g_hcommObservedArgNum = 0;
    g_hcommObservedThread = 0;
    g_hcommObservedAllocThread = 0;
    g_hcommTaskArgsNum = 3;
    g_hcommKernelLaunchResult = CCU_SUCCESS;
    g_hcommThreadAllocResult = 0;
    g_hcommNextKernelHandle = 1;
}

// 填实体 ABI 头（与 hcomm HcommCcuInstanceEntityInit 一致），实体路径头校验依赖
void InitCcuInstanceEntity(HcommCcuInstanceEntity& entity)
{
    entity.header.version = HCOMM_CCU_INSTANCE_ENTITY_ABI_VERSION;
    entity.header.magicWord = HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD;
    entity.header.size = sizeof(HcommCcuInstanceEntity);
    entity.header.reserved = 0;
}

void DummyKernel(void* arg)
{
    ++g_dummyKernelCallCount;
    g_dummyKernelObservedArg = arg;
}

asccomm_launch_kernel_cfg MakeLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    InitCcuInstanceEntity(g_defaultCcuInstance);
    g_defaultCcuInstance.ccuInsKey = 0x01U;
    cfg.ccu_ins = &g_defaultCcuInstance;
    cfg.stream = reinterpret_cast<aclrtStream>(0x22);
    cfg.attrs = nullptr;
    return cfg;
}

// 旧路径配置：ccu_ins 为小整数 token（≤ UINT32_MAX）。stream 用独立值避免与其他测试
// 共享库内按 stream 缓存的 ThreadHandle。
asccomm_launch_kernel_cfg MakeLegacyLaunchCfg()
{
    asccomm_launch_kernel_cfg cfg{};
    cfg.ccu_schd = {1, 0, 0x01, 0};
    cfg.ccu_ins = reinterpret_cast<void*>(0x11U);
    cfg.stream = reinterpret_cast<aclrtStream>(0x44);
    cfg.attrs = nullptr;
    return cfg;
}
} // namespace

// hcomm 旧接口桩：强定义覆盖 ccu_host_launch.cpp 中的 weak 未定义符号
extern "C" CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle)
{
    ++g_hcommRegisterStartCalls;
    g_hcommObservedIns = insHandle;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegister(
    CcuInsHandle insHandle, uint32_t dieId, const char*, const void*, const void**, uint32_t,
    CcuKernelHandle* kernelHandle)
{
    ++g_hcommRegisterCalls;
    g_hcommObservedIns = insHandle;
    g_hcommObservedDieId = dieId;
    *kernelHandle = g_hcommNextKernelHandle++;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle)
{
    ++g_hcommRegisterEndCalls;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle, uint32_t* taskArgsNum)
{
    ++g_hcommGetTaskArgsNumCalls;
    *taskArgsNum = g_hcommTaskArgsNum;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelLaunch(
    ThreadHandle threadHandle, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum)
{
    ++g_hcommKernelLaunchCalls;
    g_hcommObservedThread = threadHandle;
    g_hcommObservedKernelHandle = kernelHandle;
    g_hcommObservedTaskArgs = taskArgs;
    g_hcommObservedArgNum = argNum;
    return g_hcommKernelLaunchResult;
}

extern "C" HcommResult HcommThreadAllocWithStream(CommEngine, aclrtStream, uint32_t, ThreadHandle* thread)
{
    ++g_hcommThreadAllocCalls;
    *thread = 0x100U;
    g_hcommObservedAllocThread = 0x100U;
    return g_hcommThreadAllocResult;
}

class TestCcuHostLaunch : public testing::Test {
protected:
    void SetUp() override
    {
        ResetCcuLaunchStub();
        SetCcuLaunchStubTaskArgsNum(3U);
        ResetHcommStub();
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

TEST_F(TestCcuHostLaunch, NullInstanceWithBinaryCacheTagSkipsDataplane)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    cfg.ccu_schd.binary_cache_tag = 0x5701U;
    cfg.ccu_ins = nullptr; // nullptr ≤ token 上限，路由到旧路径（由真实 hcomm 报错）

    // 实体路径的数据面 stub 不应被触碰；桩不校验 ins，返回 SUCCESS
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 0U);
    EXPECT_EQ(g_hcommRegisterCalls, 1U);
    EXPECT_EQ(g_hcommObservedIns, 0U);
}

TEST_F(TestCcuHostLaunch, LaunchHashTagReturnsStableNonZeroValue)
{
    const uint64_t tag = asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel");

    EXPECT_EQ(asccomm_ccu_get_launch_hash_tag(nullptr), 0U);
    EXPECT_NE(tag, 0U);
    EXPECT_EQ(tag, asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel"));
    EXPECT_NE(tag, asccomm_ccu_get_launch_hash_tag("CcuAllGatherMesh1DMem2MemKernel_rank_1"));
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
    EXPECT_EQ(observed.lastInsHandle, cfg.ccu_ins);
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
        CcuResult::CCU_SUCCESS, CcuResult::CCU_E_INTERNAL, CcuResult::CCU_SUCCESS, CcuResult::CCU_SUCCESS,
        CcuResult::CCU_SUCCESS);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_INTERNAL);

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
        CcuResult::CCU_E_INTERNAL);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_INTERNAL);

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

TEST_F(TestCcuHostLaunch, BinaryCacheTagIsScopedToCcuInstanceAndDie)
{
    asccomm_launch_kernel_cfg firstCfg = MakeLaunchCfg();
    asccomm_launch_kernel_cfg secondCfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    constexpr uint64_t SHARED_BINARY_TAG = 0x5701U;
    HcommCcuInstanceEntity firstInstance{};
    HcommCcuInstanceEntity secondInstance{};
    InitCcuInstanceEntity(firstInstance);
    InitCcuInstanceEntity(secondInstance);
    firstCfg.ccu_schd.binary_cache_tag = SHARED_BINARY_TAG;
    secondCfg.ccu_schd.binary_cache_tag = SHARED_BINARY_TAG;
    firstInstance.ccuInsKey = 0x11U;
    firstCfg.ccu_ins = &firstInstance;
    secondInstance.ccuInsKey = 0x22U;
    secondCfg.ccu_ins = &secondInstance;

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &firstCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 1U);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &firstCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 1U);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &secondCfg, args),
        CCU_SUCCESS);
    CcuLaunchStubObservations observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerKernel, 2U);
    EXPECT_EQ(observed.lastInsHandle, secondCfg.ccu_ins);
    EXPECT_EQ(observed.lastDieId, 0U);

    asccomm_launch_kernel_cfg thirdCfg = firstCfg;
    thirdCfg.ccu_schd.phy_die_mask = 0x02U;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &thirdCfg, args),
        CCU_SUCCESS);
    observed = GetCcuLaunchStubObservations();
    EXPECT_EQ(observed.calls.registerKernel, 3U);
    EXPECT_EQ(observed.lastInsHandle, thirdCfg.ccu_ins);
    EXPECT_EQ(observed.lastDieId, 1U);
}

TEST_F(TestCcuHostLaunch, InvalidInstanceEntityHeaderReturnsPara)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    // 坏 magicWord：不应触碰数据面注册接口
    g_defaultCcuInstance.header.magicWord = 0xDEADBEEFU;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 0U);

    // 坏 size
    g_defaultCcuInstance.header.magicWord = HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD;
    g_defaultCcuInstance.header.size = 0U;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PARA);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 0U);
}

TEST_F(TestCcuHostLaunch, LegacyTokenRoutesToHcommApis)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);

    // 实体路径的数据面 stub 不应被触碰
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 0U);

    EXPECT_EQ(g_hcommRegisterStartCalls, 1U);
    EXPECT_EQ(g_hcommRegisterCalls, 1U);
    EXPECT_EQ(g_hcommRegisterEndCalls, 1U);
    EXPECT_EQ(g_hcommGetTaskArgsNumCalls, 1U);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 1U);
    EXPECT_EQ(g_hcommThreadAllocCalls, 1U);
    EXPECT_EQ(g_hcommObservedIns, 0x11U);
    EXPECT_EQ(g_hcommObservedDieId, 0U);
    EXPECT_EQ(g_hcommObservedKernelHandle, 1U);
    EXPECT_EQ(g_hcommObservedTaskArgs, args);
    EXPECT_EQ(g_hcommObservedArgNum, 3U);
    EXPECT_EQ(g_hcommObservedThread, 0x100U);
}

TEST_F(TestCcuHostLaunch, LegacyDie1MaskMapsToDieIdOne)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    cfg.ccu_schd.phy_die_mask = 0x02U;
    uint64_t args[] = {1, 2, 3};

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommObservedDieId, 1U);
}

TEST_F(TestCcuHostLaunch, LegacyThreadHandleIsCachedPerStream)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    // 用独立 stream，避免与其他测试共享库内按 stream 缓存的 ThreadHandle
    cfg.stream = reinterpret_cast<aclrtStream>(0x66);
    uint64_t args[] = {1, 2, 3};

    for (uint32_t idx = 0; idx < 2; idx++) {
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }
    // 同 stream 复用 ThreadHandle，仅首次申请
    EXPECT_EQ(g_hcommThreadAllocCalls, 1U);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 2U);

    // 换 stream 触发新申请
    cfg.stream = reinterpret_cast<aclrtStream>(0x77);
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommThreadAllocCalls, 2U);
}

TEST_F(TestCcuHostLaunch, LegacyCacheTagReusesHandleAcrossLaunches)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0x5801U;
    uint64_t args[] = {1, 2, 3};

    for (uint32_t idx = 0; idx < 2; idx++) {
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }
    // 同 (tag, token, mask) 只注册一次
    EXPECT_EQ(g_hcommRegisterCalls, 1U);
    EXPECT_EQ(g_hcommGetTaskArgsNumCalls, 2U);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 2U);
}

TEST_F(TestCcuHostLaunch, LegacyCacheTagIsScopedToTokenAndDie)
{
    asccomm_launch_kernel_cfg firstCfg = MakeLegacyLaunchCfg();
    asccomm_launch_kernel_cfg secondCfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    constexpr uint64_t SHARED_TAG = 0x5901U;
    firstCfg.ccu_schd.binary_cache_tag = SHARED_TAG;
    secondCfg.ccu_schd.binary_cache_tag = SHARED_TAG;
    secondCfg.ccu_ins = reinterpret_cast<void*>(0x22U);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &firstCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommRegisterCalls, 1U);

    // 不同 token：重新注册
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &secondCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommRegisterCalls, 2U);

    // 同 token 不同 die mask：重新注册
    asccomm_launch_kernel_cfg thirdCfg = firstCfg;
    thirdCfg.ccu_schd.phy_die_mask = 0x02U;
    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &thirdCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommRegisterCalls, 3U);
}

TEST_F(TestCcuHostLaunch, LegacyZeroCacheTagRegistersEachLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    cfg.ccu_schd.binary_cache_tag = 0U;
    uint64_t args[] = {1, 2, 3};

    for (uint32_t idx = 0; idx < 2; idx++) {
        EXPECT_EQ(
            asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
            CCU_SUCCESS);
    }
    EXPECT_EQ(g_hcommRegisterCalls, 2U);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 2U);
}

TEST_F(TestCcuHostLaunch, LegacyTaskArgsNumOverflowReturnsNotSupport)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_hcommTaskArgsNum = 14U; // 超过 SQE 13 槽上限

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_NOT_SUPPORT);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 0U);
}

TEST_F(TestCcuHostLaunch, LegacyNullStreamReturnsPtr)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    cfg.stream = nullptr;

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_PTR);
    EXPECT_EQ(g_hcommThreadAllocCalls, 0U);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 0U);
}

TEST_F(TestCcuHostLaunch, LegacyNullInstanceIsRejectedByHcommRegister)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    cfg.ccu_ins = nullptr; // nullptr ≤ token 上限，路由到旧路径，由 hcomm 侧报错

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_SUCCESS); // 桩不校验 ins，真实 hcomm 会返回 CCU_E_PTR
    EXPECT_EQ(g_hcommRegisterStartCalls, 1U);
}

TEST_F(TestCcuHostLaunch, LegacyKernelLaunchFailureIsPropagated)
{
    asccomm_launch_kernel_cfg cfg = MakeLegacyLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_hcommKernelLaunchResult = CCU_E_INTERNAL;

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &cfg, args),
        CCU_E_INTERNAL);
    EXPECT_EQ(g_hcommKernelLaunchCalls, 1U);
}

TEST_F(TestCcuHostLaunch, HcommCompatEntryConvertsFieldsAndRoutesByToken)
{
    HcommLaunchKernelCfg hcommCfg{};
    hcommCfg.ccuSchd.numBlocks = 1;
    hcommCfg.ccuSchd.reserved = 0;
    hcommCfg.ccuSchd.phyDieMask = 0x02U;
    hcommCfg.ccuSchd.binaryCacheTag = 0x6001U;
    hcommCfg.ccuIns = 0x11U;
    hcommCfg.stream = reinterpret_cast<aclrtStream>(0x22);
    hcommCfg.attrs = nullptr;
    uint64_t args[] = {1, 2, 3};

    // cfg 为 null 时等价主入口判空
    EXPECT_EQ(HcommCcuHostKernelLaunch(nullptr, DUMMY_KERNEL_NAME, &hcommCfg, args), CCU_E_PTR);

    // token 形态 ccuIns → 旧路径
    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &hcommCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(g_hcommRegisterCalls, 1U);
    EXPECT_EQ(g_hcommObservedIns, 0x11U);
    EXPECT_EQ(g_hcommObservedDieId, 1U);

    // 实体地址形态 ccuIns → 数据面路径（重建实体头：InvalidInstanceEntityHeaderReturnsPara
    // 可能已破坏全局实体的头字段）
    InitCcuInstanceEntity(g_defaultCcuInstance);
    HcommLaunchKernelCfg entityCfg = hcommCfg;
    entityCfg.ccuIns = reinterpret_cast<CcuInsHandle>(&g_defaultCcuInstance);
    EXPECT_EQ(
        HcommCcuHostKernelLaunch(reinterpret_cast<const void*>(DummyKernel), DUMMY_KERNEL_NAME, &entityCfg, args),
        CCU_SUCCESS);
    EXPECT_EQ(GetCcuLaunchStubObservations().calls.registerKernel, 1U);
    EXPECT_EQ(g_hcommRegisterCalls, 1U);
}

TEST_F(TestCcuHostLaunch, HcommGetLaunchHashTagMatchesAsccommEntry)
{
    EXPECT_EQ(HcommCcuGetLaunchHashTag("CcuKernel"), asccomm_ccu_get_launch_hash_tag("CcuKernel"));
    EXPECT_EQ(HcommCcuGetLaunchHashTag(nullptr), 0U);
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
