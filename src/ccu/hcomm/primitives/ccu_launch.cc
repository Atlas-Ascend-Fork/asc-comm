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
#include "ccu/hcomm/ccu_resource_api.h"
#include "hcomm/hcomm_ccu_launch.h"

#include <chrono>
#include <vector>

// 同步 launch 需要 acl 运行时接口（rt 相关 API），与 runtime 头配合使用
#include "acl/acl_rt.h"
#include "runtime/rt_external_kernel.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_device_context.h"
#include "ccu/hcomm/ccu_api_types.h"

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/kernel/ccu_kernel_registry_mgr.h"
#include "hcomm/resource/common/ccu_var_event_res_mgr.h"

#include "hcomm/resource/microcode/ccu_assist_v1.h"

CcuResult hcomm_ccu_kernel_register_start(
    CcuInsHandle insHandle, const HcommCcuRegisterContextPod *context)
{
    CCU_CHK_PTR_NULL(context);
    if (context->instanceHandle != 0 && context->instanceHandle != insHandle) {
        HCCL_ERROR("[%s] failed, context instanceHandle[%llu] mismatches input insHandle[%llu].", __func__,
            static_cast<unsigned long long>(context->instanceHandle), static_cast<unsigned long long>(insHandle));
        return CcuResult::CCU_E_PARA;
    }
    hcomm::CcuKernelRegistry *ccuIns = nullptr;
    CCU_CHK_RET(hcomm::CcuKernelRegistryMgr::GetInstance(context->deviceLogicId).GetOrCreate(
        context->deviceLogicId, insHandle, ccuIns));
    CCU_CHK_PTR_NULL(ccuIns);
    CCU_CHK_RET(ccuIns->BeginRegister());

    CcuResult ret = ccuIns->LoadRegisterContext(*context);
    if (ret == CcuResult::CCU_SUCCESS) {
        auto *resSnapshot = ccuIns->GetResSnapshot();
        CCU_CHK_PTR_NULL(resSnapshot);
        ret = hcomm::CcuKernelMgr::GetInstance(context->deviceLogicId).Configure(*resSnapshot);
    }
    if (ret != CcuResult::CCU_SUCCESS) {
        (void)ccuIns->EndRegister();
        HCCL_ERROR("[%s] failed to load register context, ret[%d].", __func__, ret);
        return ret;
    }
    hcomm::SetCurrentCcuDeviceLogicId(context->deviceLogicId);
    hcomm::SetCurrentCcuControlOps(ccuIns->GetResSnapshot()->GetControlOps());
    return CcuResult::CCU_SUCCESS;
}

static CcuResult CcuKernelTryRegister(
    hcomm::CcuKernelRegistry* ccuIns, hcomm::AscCcuResSnapshot* resSnapshot, uint32_t devLogicId, uint32_t dieId,
    const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs, uint32_t argNum,
    CcuKernelHandle& newHandle)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
    CCU_CHK_RET(kernelMgr.Register(*resSnapshot, dieId, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle));
    CCU_CHK_RET(ccuIns->SaveKernel(newHandle));
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_kernel_register(
    CcuInsHandle insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, CcuKernelHandle* kernelHandle)
{
    HCCL_RUN_INFO("Entry-%s", __func__);
    const auto startTime = std::chrono::steady_clock::now();

    CCU_CHK_PTR_NULL(kernelFunc);
    CCU_CHK_PTR_NULL(kernelHandle);

    if (argNum != 0) {
        CCU_CHK_PTR_NULL(kernelArgs);
    }

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto* ccuIns = hcomm::CcuKernelRegistryMgr::GetInstance(devLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(ccuIns);

    CCU_CHK_RET(ccuIns->CheckRegistering());

    auto* resSnapshot = ccuIns->GetResSnapshot();
    CCU_CHK_PTR_NULL(resSnapshot);

    CcuKernelHandle newHandle{0};
    CcuResult ret = CcuKernelTryRegister(
        ccuIns, resSnapshot, devLogicId, dieId, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle);
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
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - startTime);
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, duration.count());
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_kernel_register_end(CcuInsHandle insHandle)
{
    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto* ccuIns = hcomm::CcuKernelRegistryMgr::GetInstance(devLogicId).Get(insHandle);
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

CcuResult hcomm_ccu_get_task_args_num(CcuKernelHandle kernelHandle, uint32_t* taskArgsNum)
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

static CcuResult ConstructCcuDetailInfo(const std::vector<hcomm::CcuProfilingInfo>& allCcuProfilingInfo,
    bool isSaveProfilingData, std::vector<HcommCcuProfileDetailPod>& converted)
{
    if (allCcuProfilingInfo.empty() || !isSaveProfilingData) {
        return CcuResult::CCU_SUCCESS;
    }

    converted.resize(allCcuProfilingInfo.size());
    // profiling 明细跨动态库时只传递 POD，hcomm 在 callback 前再还原为原有 C++ 类型。
    for (uint32_t idx = 0; idx < allCcuProfilingInfo.size(); ++idx) {
        const auto& src = allCcuProfilingInfo[idx];
        auto& dst = converted[idx];
        if (src.name.size() >= HCOMM_CCU_PROFILE_NAME_CAPACITY) {
            HCCL_ERROR(
                "[%s] profiling name length[%zu] exceeds maximum[%u].", __func__, src.name.size(),
                HCOMM_CCU_PROFILE_NAME_CAPACITY - 1U);
            return CcuResult::CCU_E_PARA;
        }
        dst.header = {HCOMM_CCU_LAUNCH_ABI_VERSION, HCOMM_CCU_PROFILE_DETAIL_MAGIC_WORD, sizeof(dst), 0};
        dst.nameLength = static_cast<uint32_t>(src.name.size());
        (void)memcpy_s(dst.name, sizeof(dst.name), src.name.data(), src.name.size());
        dst.profilingType = src.type;
        dst.dieId = src.dieId;
        dst.missionId = src.missionId;
        dst.instructionId = src.instrId;
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
    return CcuResult::CCU_SUCCESS;
}

static HcommCcuTaskProfilePod ConstructCcuTaskProfile(const hcomm::CcuTaskParam& ccuParam,
    const CcuKernelHandle kernelHandle, uint64_t beginTime, uint64_t endTime, bool isMaster)
{
    // POD 头部固定携带版本、类型和大小，供 hcomm 在解释后续 profiling 字段前校验 ABI。
    HcommCcuTaskProfilePod task{};
    task.header = {HCOMM_CCU_LAUNCH_ABI_VERSION, HCOMM_CCU_TASK_PROFILE_MAGIC_WORD, sizeof(task), 0};
    task.beginCycle = beginTime;
    task.endCycle = endTime;
    task.dieId = ccuParam.dieId;
    task.missionId = ccuParam.missionId;
    task.instructionId = ccuParam.instStartId;
    task.isMaster = isMaster ? 1U : 0U;
    task.kernelHandle = kernelHandle;
    task.diagnose = hcomm::HcommCcuDiagnose;
    task.reserved = 0;
    return task;
}

static void LogCcuTaskInfo(const std::vector<hcomm::CcuTaskParam>& ccuParams, const CcuKernelHandle kernelHandle,
    uint32_t execTimeOutSec)
{
    if (CheckLogLevel(ASCCOMM_CCU_LOG_MODULE_ID, DLOG_INFO) != 1) {
        return;
    }
    for (uint32_t idx = 0; idx < ccuParams.size(); idx++) {
        const auto& param = ccuParams[idx];
        HCCL_INFO(
            "[%s] start ccu task, dieId[%u], missionId[%u], execMissionId[%u], instStartId[%u], instCnt[%u], "
            "argSize[%u], timeout[%u]s, executeId[0x%llx], ccuKernelHandle[0x%llx]",
            __func__, param.dieId, param.missionId, param.missionId, param.instStartId, param.instCnt, param.argSize,
            execTimeOutSec, kernelHandle, kernelHandle);
    }
}

static void ConstructProfilingInfoLog(const std::vector<hcomm::CcuProfilingInfo>& allCcuProfilingInfo)
{
    if (CheckLogLevel(ASCCOMM_CCU_LOG_MODULE_ID, DLOG_INFO) != 1) {
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

static CcuResult LaunchCcuTasks(
    const hcomm::CcuTaskParam& param, const aclrtStream stream, uint32_t execTimeOutSec)
{
    rtCcuTaskInfo_t taskInfo{};
    taskInfo.dieId = param.dieId;
    taskInfo.missionId = param.missionId;
    taskInfo.instStartId = param.instStartId;
    taskInfo.instCnt = param.instCnt;
    taskInfo.key = param.key;
    taskInfo.argSize = param.argSize;
    taskInfo.timeout = execTimeOutSec;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(taskInfo.args));

    auto ret = rtCCULaunch(&taskInfo, stream);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR("[%s] failed to launch ccu, ret[%d]", __func__, ret);
        return CcuResult::CCU_E_RUNTIME;
    }

    return CcuResult::CCU_SUCCESS;
}

static bool IsLaunchContextValid(const HcommCcuLaunchContextPod& context)
{
    const auto& header = context.header;
    return header.version == HCOMM_CCU_LAUNCH_ABI_VERSION
        && header.magicWord == HCOMM_CCU_LAUNCH_CONTEXT_MAGIC_WORD
        && header.size == sizeof(context) && header.reserved == 0
        && context.reserved[0] == 0 && context.reserved[1] == 0
        && context.runtimeStream != 0 && context.threadHandle != 0;
}

CcuResult hcomm_ccu_kernel_launch(
    const HcommCcuLaunchContextPod *launchContext, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum)
{
    const auto startTime = std::chrono::steady_clock::now();

    CCU_CHK_PTR_NULL(launchContext);
    CHK_PRT_RET(kernelHandle == 0, HCCL_ERROR("[%s] failed, kernel handle is empty.", __func__), CcuResult::CCU_E_PARA);
    CHK_PRT_RET(
        argNum > 0 && taskArgs == nullptr,
        HCCL_ERROR("[%s] failed, taskArgs is nullptr while argNum[%u] > 0.", __func__, argNum), CcuResult::CCU_E_PTR);

    // 跨动态库边界先完整校验 POD，避免按不兼容布局读取 stream、设备号和 profiling 状态。
    CHK_PRT_RET(!IsLaunchContextValid(*launchContext),
        HCCL_ERROR("[%s] failed, launch context has an incompatible ABI header or runtime stream.", __func__),
        CcuResult::CCU_E_PARA);
    HCCL_INFO("[hcomm_ccu_kernel_launch] threadHandle[0x%llx] kernelHandle[0x%llx].",
        launchContext->threadHandle, kernelHandle);
    // runtimeStream 是 hcomm 借出的运行时 stream，且仅在本次同步 launch 返回前有效。
    auto streamPtr = reinterpret_cast<aclrtStream>(launchContext->runtimeStream);

    // launch 使用线程实际所属设备，避免依赖 asc-comm 内部设备上下文与调用线程状态一致。
    const int32_t devLogicId = launchContext->deviceLogicId;
    auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
    auto* kernel = kernelMgr.GetKernel(kernelHandle);
    CCU_CHK_PTR_NULL(kernel);

    CCU_EXCEPTION_HANDLE_BEGIN
    std::vector<hcomm::CcuTaskParam> taskParams{};
    auto ret = kernel->GeneTaskParams(static_cast<const uint64_t*>(taskArgs), argNum, taskParams);
    CHK_PRT_RET(
        ret != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[%s] failed, threadHandle[0x%llx] kernelHandle[0x%llx].", __func__,
            launchContext->threadHandle, kernelHandle),
        ret);

    if (taskParams.empty()) {
        HCCL_INFO("[%s] passed, ccu params are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }
    const bool isSaveProfilingData = launchContext->profilingL1Enabled != 0
        || launchContext->profilingL0Enabled != 0 || launchContext->profilingCached != 0;

    std::vector<hcomm::CcuProfilingInfo> allCcuProfilingInfo;
    CCU_CHK_RET(ConstructProfilingInfo(
        kernel, static_cast<const uint64_t*>(taskArgs), argNum, allCcuProfilingInfo, isSaveProfilingData));
    LogCcuTaskInfo(taskParams, kernelHandle, launchContext->timeoutSec);
    std::vector<HcommCcuProfileDetailPod> ccuDetailInfo;
    CCU_CHK_RET(ConstructCcuDetailInfo(allCcuProfilingInfo, isSaveProfilingData, ccuDetailInfo));
    for (uint32_t idx = 0; idx < taskParams.size(); idx++) {
        uint64_t beginTime = 0;
        uint64_t endTime = 0;
        if (launchContext->getProfilingCycle != nullptr) {
            CCU_CHK_RET(launchContext->getProfilingCycle(&beginTime));
        }
        CCU_CHK_RET(LaunchCcuTasks(taskParams[idx], streamPtr, launchContext->timeoutSec));
        if (launchContext->getProfilingCycle != nullptr) {
            CCU_CHK_RET(launchContext->getProfilingCycle(&endTime));
        }
        const HcommCcuTaskProfilePod taskProfile = ConstructCcuTaskProfile(
            taskParams[idx], kernelHandle, beginTime, endTime, launchContext->isMaster != 0);
        if (launchContext->reportTask != nullptr) {
            CCU_CHK_RET(launchContext->reportTask(
                launchContext->threadHandle, &taskProfile, ccuDetailInfo.empty() ? nullptr : ccuDetailInfo.data(),
                static_cast<uint32_t>(ccuDetailInfo.size())));
        }
    }
    CCU_EXCEPTION_HANDLE_END
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - startTime);
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, duration.count());
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_get_mem_token(uint64_t srcVa, uint64_t size, uint64_t* tokenInfo)
{
    CCU_CHK_PTR_NULL(tokenInfo);

    if (srcVa == 0 || size == 0) {
        HCCL_ERROR(
            "[%s] failed, srcVa[0x%llx] size[%llu] should not be 0.", __func__,
            static_cast<unsigned long long>(srcVa), static_cast<unsigned long long>(size));
        return CcuResult::CCU_E_PARA;
    }
    // 注意token信息属于安全信息，均不允许打印
    // 查询与位域拼装收敛在 GetTokenInfo 内，此处仅转发；GetTokenInfo 会抛异常，故需异常拦截
    CCU_EXCEPTION_HANDLE_BEGIN
    *tokenInfo = hcomm::CcuRep::GetTokenInfo(srcVa, size);
    CCU_EXCEPTION_HANDLE_END

    return CcuResult::CCU_SUCCESS;
}

// 定位实例资源仓后转发给 CcuVarEventResMgr；预约、地址映射与失败回滚均由该类内部完成
static CcuResult CcuAllocVarEventRes(CcuInsHandle insHandle, hcomm::CcuVarEventType type,
    uint8_t dieId, uint32_t num, uint64_t &outHandle)
{
    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    auto *ccuIns = hcomm::CcuKernelRegistryMgr::GetInstance(devLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(ccuIns);

    auto *resPack = ccuIns->GetResSnapshot();
    CCU_CHK_PTR_NULL(resPack);

    CCU_CHK_RET(hcomm::CcuVarEventResMgr::GetInstance(devLogicId).Acquire(
        insHandle, resPack->GetCcuResRepo(), type, dieId, num, outHandle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_variable_alloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuVariableHandle *varHandle)
{
    CCU_CHK_PTR_NULL(varHandle);
    *varHandle = 0;
    CCU_CHK_RET(CcuAllocVarEventRes(insHandle, hcomm::CcuVarEventType::VARIABLE,
        dieId, num, *varHandle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_event_alloc(CcuInsHandle insHandle, uint8_t dieId, uint32_t num,
    CcuEventHandle *eventHandle)
{
    CCU_CHK_PTR_NULL(eventHandle);
    *eventHandle = 0;
    CCU_CHK_RET(CcuAllocVarEventRes(insHandle, hcomm::CcuVarEventType::EVENT,
        dieId, num, *eventHandle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_variable_get_addr(CcuVariableHandle varHandle, uint32_t index, uint64_t *va)
{
    CCU_CHK_PTR_NULL(va);

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    CCU_CHK_RET(hcomm::CcuVarEventResMgr::GetInstance(devLogicId).GetSavedAddr(
        hcomm::CcuVarEventType::VARIABLE, varHandle, index, *va));

    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_event_get_addr(CcuEventHandle eventHandle, uint32_t index, uint64_t *va)
{
    CCU_CHK_PTR_NULL(va);

    const uint32_t devLogicId = hcomm::GetCurrentCcuDeviceLogicId();
    CCU_CHK_RET(hcomm::CcuVarEventResMgr::GetInstance(devLogicId).GetSavedAddr(
        hcomm::CcuVarEventType::EVENT, eventHandle, index, *va));

    return CcuResult::CCU_SUCCESS;
}
