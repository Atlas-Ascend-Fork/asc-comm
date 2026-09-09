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

#include "acl/acl_rt.h"
#include "ccu/ccu_host_launch.h"

namespace {
constexpr uint32_t CCU_SQE_ARGS_LEN = 13;

aclError g_getDeviceResult = ACL_SUCCESS;
int32_t g_currentDeviceId = 0;
uint32_t g_getDeviceCallCount = 0;
uint32_t g_kernelMgrGetInstanceCallCount = 0;
uint32_t g_registerStartCallCount = 0;
int32_t g_observedKernelMgrDeviceId = -1;
CcuKernelHandle g_nextKernelHandle = 1;

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

extern "C" aclError aclrtGetDevice(int32_t* deviceId)
{
    ++g_getDeviceCallCount;
    if (g_getDeviceResult == ACL_SUCCESS && deviceId != nullptr) {
        *deviceId = g_currentDeviceId;
    }
    return g_getDeviceResult;
}

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
    *taskArgsNum = 0;
    return CCU_SUCCESS;
}

extern "C" int rtCCULaunch(void*, void* const) { return 0; }

namespace hcomm {
struct CcuTaskParam {
    uint8_t dieId;
    uint8_t missionId;
    uint16_t timeout;
    uint32_t instStartId;
    uint32_t instCnt;
    uint32_t key;
    uint32_t argSize;
    uint64_t args[CCU_SQE_ARGS_LEN];
};

class CcuKernel {
public:
    CcuResult GeneTaskParams(const uint64_t*, uint32_t, std::vector<CcuTaskParam>& taskParams);
};

class CcuKernelMgr {
public:
    static CcuKernelMgr& GetInstance(int32_t);

    CcuKernel* GetKernel(CcuKernelHandle);
};

CcuResult CcuKernel::GeneTaskParams(const uint64_t*, uint32_t, std::vector<CcuTaskParam>& taskParams)
{
    taskParams.push_back({});
    return CCU_SUCCESS;
}

CcuKernelMgr& CcuKernelMgr::GetInstance(int32_t deviceId)
{
    ++g_kernelMgrGetInstanceCallCount;
    g_observedKernelMgrDeviceId = deviceId;
    static CcuKernelMgr mgr;
    return mgr;
}

CcuKernel* CcuKernelMgr::GetKernel(CcuKernelHandle)
{
    static CcuKernel kernel;
    return &kernel;
}
} // namespace hcomm

class TestCcuHostLaunch : public testing::Test {
protected:
    void SetUp() override
    {
        g_getDeviceResult = ACL_SUCCESS;
        g_currentDeviceId = 0;
        g_getDeviceCallCount = 0;
        g_kernelMgrGetInstanceCallCount = 0;
        g_registerStartCallCount = 0;
        g_observedKernelMgrDeviceId = -1;
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

TEST_F(TestCcuHostLaunch, CurrentDeviceIdIsPassedToKernelManager)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_currentDeviceId = 7;

    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_getDeviceCallCount, 1U);
    EXPECT_EQ(g_kernelMgrGetInstanceCallCount, 1U);
    EXPECT_EQ(g_observedKernelMgrDeviceId, 7);
}

TEST_F(TestCcuHostLaunch, GetDeviceFailureStopsLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_getDeviceResult = static_cast<aclError>(1);

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_RUNTIME);
    EXPECT_EQ(g_kernelMgrGetInstanceCallCount, 0U);
    EXPECT_EQ(g_registerStartCallCount, 1U);
}

TEST_F(TestCcuHostLaunch, NegativeDeviceIdStopsLaunch)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};
    g_currentDeviceId = -1;

    EXPECT_EQ(
        asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_E_INTERNAL);
    EXPECT_EQ(g_kernelMgrGetInstanceCallCount, 0U);
    EXPECT_EQ(g_registerStartCallCount, 1U);
}

TEST_F(TestCcuHostLaunch, DeviceSwitchIsObservedWithoutThreadLocalCache)
{
    asccomm_launch_kernel_cfg cfg = MakeLaunchCfg();
    uint64_t args[] = {1, 2, 3};

    g_currentDeviceId = 1;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_observedKernelMgrDeviceId, 1);

    g_currentDeviceId = 2;
    EXPECT_EQ(asccomm_ccu_host_kernel_launch(reinterpret_cast<const void*>(DummyKernel), &cfg, args), CCU_SUCCESS);
    EXPECT_EQ(g_observedKernelMgrDeviceId, 2);
    EXPECT_EQ(g_getDeviceCallCount, 2U);
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
