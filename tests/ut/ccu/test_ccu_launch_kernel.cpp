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
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ccu/ccu_launch_kernel.h"

namespace {
constexpr CcuInsHandle TEST_CCU_INS = 0x11;
constexpr ThreadHandle TEST_THREAD = 0x22;
constexpr CcuKernelHandle TEST_KERNEL_HANDLE = 0x33;
constexpr uint32_t TEST_DIE_ID = 1;
constexpr uint32_t TEST_TASK_ARGS_NUM = 6;

struct FakeHcommState {
    CcuResult registerStartRet = CCU_SUCCESS;
    CcuResult registerRet = CCU_SUCCESS;
    CcuResult registerEndRet = CCU_SUCCESS;
    CcuResult getTaskArgsNumRet = CCU_SUCCESS;
    CcuResult launchRet = CCU_SUCCESS;
    CcuKernelHandle kernelHandle = TEST_KERNEL_HANDLE;
    uint32_t taskArgsNum = TEST_TASK_ARGS_NUM;

    std::vector<std::string> calls;
    CcuInsHandle registerStartIns = 0;
    CcuInsHandle registerIns = 0;
    uint32_t registerDieId = 0;
    const char *registerKernelName = nullptr;
    const void *registerKernelFunc = nullptr;
    const void **registerKernelArgs = nullptr;
    uint32_t registerKernelArgNum = 0;
    CcuInsHandle registerEndIns = 0;
    CcuKernelHandle getTaskArgsNumKernelHandle = 0;
    ThreadHandle launchThread = 0;
    CcuKernelHandle launchKernelHandle = 0;
    const void *launchTaskArgs = nullptr;
    uint32_t launchArgNum = 0;
};

FakeHcommState g_fakeState;

void ResetFakeHcommState()
{
    g_fakeState = FakeHcommState{};
}

void DummyKernel()
{
}

AscCommCcuLaunchCfg MakeLaunchCfg()
{
    AscCommCcuLaunchCfg cfg{};
    cfg.ccuSchd = {1, 0, TEST_DIE_ID};
    cfg.ccuIns = TEST_CCU_INS;
    cfg.thread = TEST_THREAD;
    cfg.attrs = nullptr;
    cfg.kernelName = "DummyKernel";
    return cfg;
}

AscCommCcuLaunchArgs MakeLaunchArgs(const void **kernelArgs, const void *taskArgs)
{
    AscCommCcuLaunchArgs args{};
    args.taskArgs = taskArgs;
    args.kernelArgs = kernelArgs;
    return args;
}
} // namespace

extern "C" CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle)
{
    g_fakeState.calls.push_back("register_start");
    g_fakeState.registerStartIns = insHandle;
    return g_fakeState.registerStartRet;
}

extern "C" CcuResult HcommCcuKernelRegister(CcuInsHandle insHandle, uint32_t dieId,
    const char *kernelFuncName, const void *kernelFunc, const void **kernelArgs,
    uint32_t argNum, CcuKernelHandle *kernelHandle)
{
    g_fakeState.calls.push_back("register");
    g_fakeState.registerIns = insHandle;
    g_fakeState.registerDieId = dieId;
    g_fakeState.registerKernelName = kernelFuncName;
    g_fakeState.registerKernelFunc = kernelFunc;
    g_fakeState.registerKernelArgs = kernelArgs;
    g_fakeState.registerKernelArgNum = argNum;
    *kernelHandle = g_fakeState.kernelHandle;
    return g_fakeState.registerRet;
}

extern "C" CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle)
{
    g_fakeState.calls.push_back("register_end");
    g_fakeState.registerEndIns = insHandle;
    return g_fakeState.registerEndRet;
}

extern "C" CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t *taskArgsNum)
{
    g_fakeState.calls.push_back("get_task_args_num");
    g_fakeState.getTaskArgsNumKernelHandle = kernelHandle;
    *taskArgsNum = g_fakeState.taskArgsNum;
    return g_fakeState.getTaskArgsNumRet;
}

extern "C" CcuResult HcommCcuKernelLaunch(ThreadHandle threadHandle,
    CcuKernelHandle kernelHandle, const void *taskArgs, uint32_t argNum)
{
    g_fakeState.calls.push_back("launch");
    g_fakeState.launchThread = threadHandle;
    g_fakeState.launchKernelHandle = kernelHandle;
    g_fakeState.launchTaskArgs = taskArgs;
    g_fakeState.launchArgNum = argNum;
    return g_fakeState.launchRet;
}

class AscCommCcuLaunchKernelTest : public testing::Test {
protected:
    void SetUp() override
    {
        ResetFakeHcommState();
    }
};

TEST_F(AscCommCcuLaunchKernelTest, NullArgumentsReturnPtr)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);

    EXPECT_EQ(AscCommCcuLaunchKernel(nullptr, &cfg, &args), CCU_E_PTR);
    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), nullptr, &args), CCU_E_PTR);
    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, nullptr), CCU_E_PTR);
    EXPECT_TRUE(g_fakeState.calls.empty());
}

TEST_F(AscCommCcuLaunchKernelTest, SuccessRegistersQueriesTaskArgNumAndLaunches)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_SUCCESS);

    const std::vector<std::string> expectedCalls = {
        "register_start", "register", "register_end", "get_task_args_num", "launch"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
    EXPECT_EQ(g_fakeState.registerStartIns, TEST_CCU_INS);
    EXPECT_EQ(g_fakeState.registerIns, TEST_CCU_INS);
    EXPECT_EQ(g_fakeState.registerDieId, TEST_DIE_ID);
    EXPECT_STREQ(g_fakeState.registerKernelName, cfg.kernelName);
    EXPECT_EQ(g_fakeState.registerKernelFunc, reinterpret_cast<const void *>(DummyKernel));
    EXPECT_EQ(g_fakeState.registerKernelArgs, kernelArgs);
    EXPECT_EQ(g_fakeState.registerKernelArgNum, 1U);
    EXPECT_EQ(g_fakeState.registerEndIns, TEST_CCU_INS);
    EXPECT_EQ(g_fakeState.getTaskArgsNumKernelHandle, TEST_KERNEL_HANDLE);
    EXPECT_EQ(g_fakeState.launchThread, TEST_THREAD);
    EXPECT_EQ(g_fakeState.launchKernelHandle, TEST_KERNEL_HANDLE);
    EXPECT_EQ(g_fakeState.launchTaskArgs, taskArgs);
    EXPECT_EQ(g_fakeState.launchArgNum, TEST_TASK_ARGS_NUM);
}

TEST_F(AscCommCcuLaunchKernelTest, RegisterStartFailureReturnsDirectly)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);
    g_fakeState.registerStartRet = CCU_E_INTERNAL;

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_E_INTERNAL);

    const std::vector<std::string> expectedCalls = {"register_start"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
}

TEST_F(AscCommCcuLaunchKernelTest, RegisterFailureCallsRegisterEndAndReturnsError)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);
    g_fakeState.registerRet = CCU_E_PARA;

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_E_PARA);

    const std::vector<std::string> expectedCalls = {"register_start", "register", "register_end"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
    EXPECT_EQ(g_fakeState.registerEndIns, TEST_CCU_INS);
}

TEST_F(AscCommCcuLaunchKernelTest, RegisterEndFailureSkipsTaskArgQueryAndLaunch)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);
    g_fakeState.registerEndRet = CCU_E_INTERNAL;

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_E_INTERNAL);

    const std::vector<std::string> expectedCalls = {"register_start", "register", "register_end"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
}

TEST_F(AscCommCcuLaunchKernelTest, GetTaskArgsNumFailureSkipsLaunch)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);
    g_fakeState.getTaskArgsNumRet = CCU_E_INTERNAL;

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_E_INTERNAL);

    const std::vector<std::string> expectedCalls = {
        "register_start", "register", "register_end", "get_task_args_num"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
}

TEST_F(AscCommCcuLaunchKernelTest, LaunchFailureReturnsLaunchError)
{
    int32_t kernelArg = 0;
    const void *kernelArgs[] = {&kernelArg};
    uint64_t taskArgs[] = {1, 2, 3};
    AscCommCcuLaunchCfg cfg = MakeLaunchCfg();
    AscCommCcuLaunchArgs args = MakeLaunchArgs(kernelArgs, taskArgs);
    g_fakeState.launchRet = CCU_E_INTERNAL;

    EXPECT_EQ(AscCommCcuLaunchKernel(reinterpret_cast<const void *>(DummyKernel), &cfg, &args), CCU_E_INTERNAL);

    const std::vector<std::string> expectedCalls = {
        "register_start", "register", "register_end", "get_task_args_num", "launch"};
    EXPECT_EQ(g_fakeState.calls, expectedCalls);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
