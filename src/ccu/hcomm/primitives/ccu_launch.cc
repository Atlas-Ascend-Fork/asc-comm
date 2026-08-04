/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_res.h"

#include <vector>

#include "ccu_device_res.h"

#include "hcomm/common/ccu_log.h"
#include "ccu/hcomm/ccu_types.h"
#include "hcomm/common/ccu_device_context.h"   // GetCurrentCcuDeviceLogicId

#include "hcomm/common/ccu_runtime.h"   // AsccommGetDevice/SetDevice/GetTaskIdAndStreamId/QueryUbToken
#include "hcomm_ccu_launch.h"           // pkg_inc: HcommCcuThreadGetLaunchContext + POD
#include "hcomm_ccu_profile_pod.h"      // pkg_inc: HcommCcuTaskProfilePod / HcommCcuProfileDetailPod

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/instance/ccu_instance_mgr.h"

#include "hcomm/resource/microcode/ccu_assist_v1.h"

#include "unified_platform/pub_inc/config_plf_log.h"
using Hccl::PLF_TASK;

CcuResult HcommCcuKernelRegisterStart(
    CcuInsHandle insHandle, const HcommCcuRegisterContextPod *context)
{
    CCU_CHK_PTR_NULL(context);
    auto *ccuIns = hcomm::CcuInstanceMgr::GetInstance(context->deviceLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(ccuIns);
    CCU_CHK_RET(ccuIns->BeginRegister());

    CcuResult ret = ccuIns->LoadRegisterContext(*context);
    if (ret == CcuResult::CCU_SUCCESS) {
        auto *resPack = ccuIns->GetResPack();
        CCU_CHK_PTR_NULL(resPack);
        ret = hcomm::CcuKernelMgr::GetInstance(context->deviceLogicId).Configure(*resPack);
    }
    if (ret != CcuResult::CCU_SUCCESS) {
        (void)ccuIns->EndRegister();
        HCCL_ERROR("[%s] failed to load register context, ret[%d].", __func__, ret);
        return ret;
    }
    hcomm::SetCurrentCcuDeviceLogicId(context->deviceLogicId);
    return CcuResult::CCU_SUCCESS;
}

static CcuResult CcuKernelTryRegister(
    hcomm::CcuInstance* ccuIns, hcomm::CcuResPack* resPack, uint32_t devLogicId, uint32_t dieId,
    const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs, uint32_t argNum,
    CcuKernelHandle& newHandle)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
    CCU_CHK_RET(kernelMgr.Register(*resPack, dieId, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle));
    CCU_CHK_RET(ccuIns->SaveKernel(newHandle));
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuKernelRegister(
    CcuInsHandle insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, CcuKernelHandle* kernelHandle)
{
    HCCL_RUN_INFO("Entry-%s", __func__);
    HcclUs startut = TIME_NOW();

    CCU_CHK_PTR_NULL(kernelFunc);
    CCU_CHK_PTR_NULL(kernelHandle);

    if (argNum != 0) {
        CCU_CHK_PTR_NULL(kernelArgs);
    }

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto* ccuIns = hcomm::CcuInstanceMgr::GetInstance(devLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(ccuIns);

    CCU_CHK_RET(ccuIns->CheckRegistering());

    auto* resPack = ccuIns->GetResPack();
    CCU_CHK_PTR_NULL(resPack);

    CcuKernelHandle newHandle{0};
    CcuResult ret = CcuKernelTryRegister(
        ccuIns, resPack, devLogicId, dieId, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle);
    if (ret != CcuResult::CCU_SUCCESS) {
        ccuIns->AbortRegister();
        if (CCU_CHK_RES_UNAVAIL(ret)) {
            HCCL_WARNING(
                "[%s] register kernel resource unavailable[%d], current register round aborted.", __func__, ret);
            return CcuResult::CCU_E_UNAVAIL;
        } else {
            HCCL_ERROR("[%s] failed, register kernel failed[%d], current register round aborted.", __func__, ret);
            return ret;
        }
    }

    *kernelHandle = newHandle;
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, DURATION_US(TIME_NOW() - startut).count());
    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle)
{
    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto* ccuIns = hcomm::CcuInstanceMgr::GetInstance(devLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(ccuIns);

    CCU_CHK_RET(ccuIns->EndRegister());
    const auto& newKernels = ccuIns->GetUntranslatedKernels();

    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
    // 当前翻译内部流程可能抛异常
    CCU_EXCEPTION_HANDLE_BEGIN
    CCU_CHK_RET(kernelMgr.Translate(newKernels));
    CCU_EXCEPTION_HANDLE_END

    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t* taskArgsNum)
{
    HCCL_INFO("Entry-%s", __func__);
    CCU_CHK_PTR_NULL(taskArgsNum);

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);

    hcomm::CcuKernelInfo info{};
    CCU_CHK_RET(kernelMgr.GetCcuKernelInfo(kernelHandle, info));

    *taskArgsNum = info.maxTaskArgsNum;
    HCCL_INFO("[%s] success, kernelHandle[%llx], taskArgsNum[%u].", __func__, kernelHandle, *taskArgsNum);
    return CcuResult::CCU_SUCCESS;
}

static std::shared_ptr<std::vector<Hccl::CcuProfilingInfo>> ConstructCcuDetailInfo(
    const std::vector<hcomm::CcuProfilingInfo>& allCcuProfilingInfo, bool isSaveProfilingData)
{
    if (allCcuProfilingInfo.empty() || !isSaveProfilingData) {
        return nullptr;
    }

    auto converted = std::make_shared<std::vector<HcommCcuProfileDetailPod>>();
    converted->resize(allCcuProfilingInfo.size());
    for (u32 idx = 0; idx < allCcuProfilingInfo.size(); ++idx) {
        const auto& src = allCcuProfilingInfo[idx];
        auto& dst = (*converted)[idx];
        (void)memset_s(&dst, sizeof(dst), 0, sizeof(dst));
        // name 超 HCOMM_CCU_PROFILE_NAME_MAX_LEN 直接报错（不静默截断）
        if (src.name.size() > HCOMM_CCU_PROFILE_NAME_MAX_LEN) {
            HCCL_ERROR("[%s] profiling info[%u] name len[%zu] exceeds max[%u], name[%s].", __func__, idx,
                src.name.size(), HCOMM_CCU_PROFILE_NAME_MAX_LEN, src.name.c_str());
            return nullptr;
        }
        (void)memcpy_s(dst.name, sizeof(dst.name), src.name.data(), src.name.size());
        dst.type = src.type;
        dst.dieId = src.dieId;
        dst.missionId = src.missionId;
        dst.instrId = src.instrId;
        dst.reduceOpType = src.reduceOpType;
        dst.inputDataType = src.inputDataType;
        dst.outputDataType = src.outputDataType;
        dst.dataSize = src.dataSize;
        dst.ckeId = src.ckeId;
        dst.mask = src.mask;
        (void)memcpy_s(dst.channelId, sizeof(dst.channelId), src.channelId, sizeof(src.channelId));
        (void)memcpy_s(dst.remoteRankId, sizeof(dst.remoteRankId), src.remoteRankId, sizeof(src.remoteRankId));
        (void)memcpy_s(dst.channelHandle, sizeof(dst.channelHandle), src.channelHandle, sizeof(src.channelHandle));
    }
    return converted;
}

static HcommCcuTaskProfilePod ConstructCcuTaskPod(
    const hcomm::CcuTaskParam& ccuParam, const CcuKernelHandle kernelHandle, u64 beginTime, u64 endTime,
    uint32_t isMaster)
{
    HcommCcuTaskProfilePod taskPod{};
    taskPod.taskType = HCOMM_CCU_TASK_TYPE_CCU;
    taskPod.beginTime = beginTime;
    taskPod.endTime = endTime;
    taskPod.isMaster = isMaster;
    taskPod.dieId = ccuParam.dieId;
    taskPod.missionId = ccuParam.missionId;
    taskPod.execMissionId = ccuParam.missionId;
    taskPod.instrId = ccuParam.instStartId;
    taskPod.executeId = kernelHandle;
    taskPod.ccuKernelHandle = kernelHandle;
    return taskPod;
}

static void LogCcuTaskInfo(const std::vector<hcomm::CcuTaskParam>& ccuParams, const CcuKernelHandle kernelHandle,
    uint32_t timeoutSec)
{
    if (!HcclCheckLogLevel(HCCL_LOG_INFO)) {
        return;
    }
    const uint32_t execTimeOutSec = timeoutSec;
    for (u32 idx = 0; idx < ccuParams.size(); idx++) {
        const auto& param = ccuParams[idx];
        PLF_CONFIG_INFO(
            PLF_TASK,
            "[%s] start ccu task, dieId[%u], missionId[%u], execMissionId[%u], instStartId[%u], instCnt[%u], "
            "argSize[%u], timeout[%u]s, executeId[0x%llx], ccuKernelHandle[0x%llx]",
            __func__, param.dieId, param.missionId, param.missionId, param.instStartId, param.instCnt, param.argSize,
            execTimeOutSec, kernelHandle, kernelHandle);
    }
}

static void ConstructProfilingInfoLog(const std::vector<hcomm::CcuProfilingInfo>& allCcuProfilingInfo)
{
    if (!HcclCheckLogLevel(HCCL_LOG_INFO)) {
        return;
    }
    for (const hcomm::CcuProfilingInfo& profInfo : allCcuProfilingInfo) {
        for (int idx = 0; idx < hcomm::CCU_MAX_CHANNEL_NUM; idx++) {
            if (profInfo.channelId[idx] == hcomm::INVALID_VALUE_CHANNELID) {
                break;
            }
            HCCL_INFO(
                "[%s]idx[%d]: channelId[%u], channelHandle[0x%llx]", __func__, idx, profInfo.channelId[idx],
                profInfo.channelHandle[idx]);
        }
    }
}

static CcuResult ConstructProfilingInfo(
    hcomm::CcuKernel* kernel, const uint64_t* taskArgs, uint32_t argNum,
    std::vector<hcomm::CcuProfilingInfo>& allCcuProfilingInfo, bool isSaveProfilingData)
{
    if (!isSaveProfilingData) {
        return CcuResult::CCU_SUCCESS;
    }

    CCU_CHK_RET(kernel->GetCcuProfilingInfo(taskArgs, argNum, allCcuProfilingInfo));
    if (allCcuProfilingInfo.empty()) {
        return CcuResult::CCU_SUCCESS;
    }
    ConstructProfilingInfoLog(allCcuProfilingInfo);
    return CcuResult::CCU_SUCCESS;
}

static CcuResult ReportCcuTaskDfx(const HcommCcuLaunchContextPod &ctx, const ThreadHandle threadHandle,
    const HcommCcuTaskProfilePod &taskPod,
    const std::shared_ptr<std::vector<HcommCcuProfileDetailPod>> &ccuDetailInfo)
{
    // reportTask 为空表示 thread 未注册 task info callback，跳过上报
    if (ctx.reportTask == nullptr) {
        HCCL_WARNING("[%s] task info callback is not registered on thread, skip ccu profiling report.", __func__);
        return CcuResult::CCU_SUCCESS;
    }
    uint32_t streamId = 0;
    uint32_t taskId = 0;
    CCU_CHK_RET(hcomm::AsccommGetTaskIdAndStreamId(taskId, streamId));
    HcommResult ret = ctx.reportTask(threadHandle, streamId, taskId, &taskPod,
        (ccuDetailInfo != nullptr) ? ccuDetailInfo->data() : nullptr,
        (ccuDetailInfo != nullptr) ? static_cast<uint32_t>(ccuDetailInfo->size()) : 0U);
    return static_cast<CcuResult>(ret);
}

static HcclResult LaunchCcuTasks(const hcomm::CcuTaskParam& param, const aclrtStream stream, uint32_t timeoutSec)
{
    rtCcuTaskInfo_t taskInfo{};
    taskInfo.dieId = param.dieId;
    taskInfo.missionId = param.missionId;
    taskInfo.instStartId = param.instStartId;
    taskInfo.instCnt = param.instCnt;
    taskInfo.key = param.key;
    taskInfo.argSize = param.argSize;
    taskInfo.timeout = timeoutSec;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(taskInfo.args));

    auto ret = rtCCULaunch(&taskInfo, stream);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR("[%s] failed to launch ccu, ret[%d]", __func__, ret);
        return HcclResult::HCCL_E_RUNTIME;
    }

    return HcclResult::HCCL_SUCCESS;
}

CcuResult HcommCcuKernelLaunch(
    ThreadHandle threadHandle, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum)
{
    const auto& startus = TIME_NOW();

    CHK_PRT_RET(threadHandle == 0, HCCL_ERROR("[%s] failed, thread handle is empty.", __func__), CcuResult::CCU_E_PARA);
    CHK_PRT_RET(kernelHandle == 0, HCCL_ERROR("[%s] failed, kernel handle is empty.", __func__), CcuResult::CCU_E_PARA);
    CHK_PRT_RET(
        argNum > 0 && taskArgs == nullptr,
        HCCL_ERROR("[%s] failed, taskArgs is nullptr while argNum[%u] > 0.", __func__, argNum), CcuResult::CCU_E_PTR);

    PLF_CONFIG_INFO(
        PLF_TASK, "[HcommCcuKernelLaunch] threadHandle[0x%llx] kernelHandle[0x%llx].", threadHandle, kernelHandle);

    // 获取一次性 launch 上下文快照（标量 + 函数指针），不再 reinterpret_cast<hccl::Thread*>
    HcommCcuLaunchContextPod ctx{};
    HcommResult ctxRet = HcommCcuThreadGetLaunchContext(threadHandle, &ctx);
    CHK_PRT_RET(ctxRet != static_cast<HcommResult>(0),
        HCCL_ERROR("[%s] HcommCcuThreadGetLaunchContext failed, ret[%d].", __func__, static_cast<int>(ctxRet)),
        static_cast<CcuResult>(ctxRet));

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    // runtime stream 为无所有权 opaque value，仅交给公开 runtime C API（rtCCULaunch）
    if (ctx.runtimeStream == 0U) {
        HCCL_ERROR("[%s] failed, runtime stream is empty.", __func__);
        return CcuResult::CCU_E_PTR;
    }
    aclrtStream streamPtr = reinterpret_cast<aclrtStream>(ctx.runtimeStream);
    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
    auto* kernel = kernelMgr.GetKernel(kernelHandle);
    CCU_CHK_PTR_NULL(kernel);

    CCU_EXCEPTION_HANDLE_BEGIN
    std::vector<hcomm::CcuTaskParam> taskParams{};
    auto ret = kernel->GeneTaskParams(static_cast<const uint64_t*>(taskArgs), argNum, taskParams);
    CHK_PRT_RET(
        ret != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[%s] failed, threadHandle[0x%llx] kernelHandle[0x%llx].", __func__, threadHandle, kernelHandle),
        ret);

    if (taskParams.empty()) {
        HCCL_INFO("[%s] passed, ccu params are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }
    // profiling 开关由 launch context 标量快照提供，不再调 hcomm ProfilingHandler 单例
    bool isProfilingEnabledL1 = (ctx.profilingFlags & HCOMM_CCU_PROFILING_FLAG_L1) != 0U;
    bool isProfilingEnabledL0 = (ctx.profilingFlags & HCOMM_CCU_PROFILING_FLAG_L0) != 0U;
    bool isOpbase = (ctx.profilingFlags & HCOMM_CCU_PROFILING_FLAG_OPBASE) != 0U;
    bool isSaveProfilingData = !(!isProfilingEnabledL1 && !isProfilingEnabledL0 && isOpbase);

    std::vector<hcomm::CcuProfilingInfo> allCcuProfilingInfo;
    CCU_CHK_RET(ConstructProfilingInfo(
        kernel, static_cast<const uint64_t*>(taskArgs), argNum, allCcuProfilingInfo, isSaveProfilingData));
    LogCcuTaskInfo(taskParams, kernelHandle, ctx.timeoutSec);
    auto ccuDetailInfo = ConstructCcuDetailInfoPod(allCcuProfilingInfo, isSaveProfilingData);
    for (u32 idx = 0; idx < taskParams.size(); idx++) {
        // 时间戳：优先用 launch context 的 getCycleTime 函数指针，回退公开 API
        u64 beginTime = (ctx.getCycleTime != nullptr) ? ctx.getCycleTime() : 0U;
        CCU_CHK_RET(LaunchCcuTasks(taskParams[idx], streamPtr, ctx.timeoutSec));
        u64 endTime = (ctx.getCycleTime != nullptr) ? ctx.getCycleTime() : 0U;
        HcommCcuTaskProfilePod taskPod =
            ConstructCcuTaskPod(taskParams[idx], kernelHandle, beginTime, endTime, ctx.isMaster);
        CCU_CHK_RET(ReportCcuTaskDfx(ctx, threadHandle, taskPod, ccuDetailInfo));
    }
    CCU_EXCEPTION_HANDLE_END
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, DURATION_US(TIME_NOW() - startus).count());
    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t* tokenInfo)
{
    CCU_CHK_PTR_NULL(tokenInfo);

    if (srcVa == 0 || size == 0) {
        HCCL_ERROR(
            "[%s] failed, srcVa[0x%llx] size[%llu] should not be 0.", __func__, static_cast<unsigned long long>(srcVa),
            static_cast<unsigned long long>(size));
        return CcuResult::CCU_E_PARA;
    }
    // 注意token信息属于安全信息，均不允许打印
    uint32_t tokenId = 0;
    uint32_t tokenValue = 0;
    // CANN 无公开 UB token 查询接口，经 pkg_inc 窄 ABI 由 hcomm adapter 实现
    CCU_CHK_RET(hcomm::AsccommQueryUbToken(srcVa, size, tokenId, tokenValue));
    *tokenInfo = hcomm::CcuRep::CcuCombineTokenInfo(tokenId, tokenValue, 1);

    return CcuResult::CCU_SUCCESS;
}
