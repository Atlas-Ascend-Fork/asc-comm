#ifndef CCU_LAUNCH_STUB_H
#define CCU_LAUNCH_STUB_H

#include "ccu/hcomm/ccu_launch.h"

struct CcuLaunchStubCalls {
    uint32_t registerStart;
    uint32_t registerKernel;
    uint32_t registerEnd;
    uint32_t getTaskArgsNum;
    uint32_t kernelLaunch;
};

struct CcuLaunchStubObservations {
    CcuLaunchStubCalls calls{};
    CcuInsHandle lastInsHandle{};
    uint32_t lastDieId{};
    const char* lastKernelName{};
    const void* lastKernelFunc{};
    const void* lastKernelArg{};
    ccu_kernel_handle lastKernelHandle{};
    uint32_t lastTaskArgsNum{};
    ccu_launch_stream lastStream{};
    const void* lastTaskArgs{};
    uint32_t lastLaunchArgNum{};
};

void ResetCcuLaunchStub();
void SetCcuLaunchStubResults(
    CcuResult registerStart, CcuResult registerKernel, CcuResult registerEnd, CcuResult getTaskArgsNum,
    CcuResult kernelLaunch);
void SetCcuLaunchStubTaskArgsNum(uint32_t taskArgsNum);
CcuLaunchStubObservations GetCcuLaunchStubObservations();

#endif // CCU_LAUNCH_STUB_H
