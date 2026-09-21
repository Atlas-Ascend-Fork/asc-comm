/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_host_launch.h"

#include "rt_external_kernel.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xxhash_impl.inc"

int32_t HcclGetThreadDeviceId();

#ifndef HCOMM_WEAK_SYMBOL
#define HCOMM_WEAK_SYMBOL __attribute__((weak))
#endif

#ifndef ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES
typedef uint64_t CcuKernelHandle;
typedef void* CcuKernelArg;
#endif

extern "C" {
extern CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegister(
    CcuInsHandle insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, CcuKernelHandle* kernelHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t* taskArgsNum) HCOMM_WEAK_SYMBOL;
}

namespace hcomm {
constexpr uint32_t CCU_SQE_ARGS_LEN = 13;

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
    CcuResult GeneTaskParams(const uint64_t* taskArgs, uint32_t argsNum, std::vector<CcuTaskParam>& taskParams);
};

class CcuKernelMgr {
public:
    static CcuKernelMgr& GetInstance(int32_t deviceLogicId);
    CcuKernel* GetKernel(CcuKernelHandle kernelHandle);
};
} // namespace hcomm

namespace {
constexpr uint32_t NOTIFY_DEFAULT_WAIT_TIME = 27U * 68U; // notifywait默认1836等待时长
constexpr uint64_t CCU_DIE0_MASK = 0x01U;
constexpr uint64_t CCU_DIE1_MASK = 0x02U;
constexpr uint32_t CCU_DIE0_ID = 0U;
constexpr uint32_t CCU_DIE1_ID = 1U;
constexpr uint32_t CCU_SUPPORTED_NUM_BLOCKS = 1U;

bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto getTaskArgsNum = HcommCcuGetTaskArgsNum;
    return registerStart != nullptr && registerKernel != nullptr && registerEnd != nullptr && getTaskArgsNum != nullptr;
}

bool IsSupportedSingleDieMask(uint64_t phyDieMask)
{
    return phyDieMask == CCU_DIE0_MASK || phyDieMask == CCU_DIE1_MASK;
}

uint32_t GetDieIdByMask(uint64_t phyDieMask) { return phyDieMask == CCU_DIE1_MASK ? CCU_DIE1_ID : CCU_DIE0_ID; }

CcuResult ValidateLaunchCfg(const asccomm_launch_kernel_cfg* cfg)
{
    if (cfg->ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS) {
        return CCU_E_PARA;
    }
    if (!IsSupportedSingleDieMask(cfg->ccu_schd.phy_die_mask)) {
        return CCU_E_PARA;
    }
    return CCU_SUCCESS;
}

using VoidPackedCcuKernel = void (*)(void*);

struct VoidKernelRegisterCtx {
    const void* kernelFunc;
    void* packedArgs;
};

CcuResult VoidKernelTrampoline(CcuKernelArg arg);

CcuResult RegisterCcuKernel(
    const void* kernelFunc, const asccomm_launch_kernel_cfg* cfg, void* args, CcuKernelHandle& kernelHandle)
{
    CcuResult ret = HcommCcuKernelRegisterStart(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    const uint32_t dieId = GetDieIdByMask(cfg->ccu_schd.phy_die_mask);
    VoidKernelRegisterCtx registerCtx{
        kernelFunc,
        args,
    };

    const void* kernelArgs[] = {&registerCtx};

    // kernelFunc do not support func that return void , only support return CcuResult
    ret = HcommCcuKernelRegister(
        cfg->ccu_ins, dieId, nullptr, reinterpret_cast<const void*>(VoidKernelTrampoline), kernelArgs, 1,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        (void)HcommCcuKernelRegisterEnd(cfg->ccu_ins);
        return ret;
    }

    return HcommCcuKernelRegisterEnd(cfg->ccu_ins);
}

class KernelHandleCache {
public:
    CcuResult Match(
        const void* kernelFunc, const asccomm_launch_kernel_cfg* cfg, void* args, CcuKernelHandle& kernelHandle,
        uint32_t& taskArgsNum)
    {
        const uint64_t cacheTag = cfg->ccu_schd.binary_cache_tag;
        if (cacheTag == 0U) {
            return RegisterAndGetTaskArgsNum(kernelFunc, cfg, args, kernelHandle, taskArgsNum);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cacheTag);
        if (iter != cache_.end()) {
            kernelHandle = iter->second;
            return HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        }

        CcuResult ret = RegisterAndGetTaskArgsNum(kernelFunc, cfg, args, kernelHandle, taskArgsNum);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        cache_[cacheTag] = kernelHandle;
        return CCU_SUCCESS;
    }

private:
    CcuResult RegisterAndGetTaskArgsNum(
        const void* kernelFunc, const asccomm_launch_kernel_cfg* cfg, void* args, CcuKernelHandle& kernelHandle,
        uint32_t& taskArgsNum)
    {
        CcuResult ret = RegisterCcuKernel(kernelFunc, cfg, args, kernelHandle);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        ret = HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        return CCU_SUCCESS;
    }

    std::mutex mutex_;
    std::unordered_map<uint64_t, CcuKernelHandle> cache_;
};

KernelHandleCache& GetKernelHandleCache()
{
    static KernelHandleCache cache;
    return cache;
}

CcuResult VoidKernelTrampoline(CcuKernelArg arg)
{
    auto* ctx = static_cast<VoidKernelRegisterCtx*>(arg);
    if (ctx == nullptr || ctx->kernelFunc == nullptr || ctx->packedArgs == nullptr) {
        return CCU_E_PTR;
    }

    auto fn = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void*>(ctx->kernelFunc));
    fn(ctx->packedArgs);
    return CCU_SUCCESS;
}

CcuResult GetSingleTaskParam(
    CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum, hcomm::CcuTaskParam& taskParam)
{
    if (kernelHandle == 0) {
        return CCU_E_PARA;
    }
    if (argNum > hcomm::CCU_SQE_ARGS_LEN) {
        return CCU_E_NOT_SUPPORT;
    }
    if (argNum > 0 && taskArgs == nullptr) {
        return CCU_E_PTR;
    }

    try {
        const uint32_t devLogicId = static_cast<uint32_t>(HcclGetThreadDeviceId());
        auto& kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
        auto* kernel = kernelMgr.GetKernel(kernelHandle);
        if (kernel == nullptr) {
            return CCU_E_PTR;
        }

        std::vector<hcomm::CcuTaskParam> taskParams{};
        CcuResult ret = kernel->GeneTaskParams(static_cast<const uint64_t*>(taskArgs), argNum, taskParams);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        if (taskParams.size() != 1) {
            return CCU_E_NOT_SUPPORT;
        }

        taskParam = taskParams.front();
    } catch (...) {
        return CCU_E_INTERNAL;
    }

    return CCU_SUCCESS;
}

CcuResult LaunchSingleCcuTask(const hcomm::CcuTaskParam& param, aclrtStream stream)
{
    if (stream == nullptr) {
        return CCU_E_PTR;
    }

    rtCcuTaskInfo_t taskInfo{};
    taskInfo.dieId = param.dieId;
    taskInfo.missionId = param.missionId;
    taskInfo.instStartId = param.instStartId;
    taskInfo.instCnt = param.instCnt;
    taskInfo.key = param.key;
    taskInfo.argSize = param.argSize;
    taskInfo.timeout = NOTIFY_DEFAULT_WAIT_TIME;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(taskInfo.args));

    auto rtRet = rtCCULaunch(&taskInfo, stream);
    if (rtRet != RT_ERROR_NONE) {
        return CCU_E_RUNTIME;
    }

    return CCU_SUCCESS;
}

} // namespace

extern "C" uint64_t asccomm_ccu_get_launch_hash_tag(const char* tag)
{
    if (tag == nullptr) {
        return 0;
    }
    return static_cast<uint64_t>(XXH3_64bits(tag, std::strlen(tag)));
}

extern "C" ccu_result asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const asccomm_launch_kernel_cfg* cfg, void* args)
{
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        return CCU_E_PTR;
    }
    CcuResult ret = ValidateLaunchCfg(cfg);
    if (ret != CCU_SUCCESS) {
        return ret;
    }
    if (!IsCcuKernelLaunchApiAvailable()) {
        return CCU_E_NOT_SUPPORT;
    }

    CcuKernelHandle kernelHandle = 0;
    uint32_t taskArgsNum = 0;
    ret = GetKernelHandleCache().Match(kernel_func, cfg, args, kernelHandle, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    hcomm::CcuTaskParam taskParam{};
    ret = GetSingleTaskParam(kernelHandle, args, taskArgsNum, taskParam);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    return LaunchSingleCcuTask(taskParam, cfg->stream);
}

extern "C" CcuResult HcommCcuHostKernelLaunch(const void* kernel_func, const HcommLaunchKernelCfg* cfg, void* args)
{
    if (cfg == nullptr) {
        return asccomm_ccu_host_kernel_launch(kernel_func, nullptr, args);
    }

    asccomm_launch_kernel_cfg asccommCfg{};
    asccommCfg.ccu_schd.num_blocks = cfg->ccuSchd.numBlocks;
    asccommCfg.ccu_schd.reserved = cfg->ccuSchd.reserved;
    asccommCfg.ccu_schd.phy_die_mask = cfg->ccuSchd.phyDieMask;
    asccommCfg.ccu_schd.binary_cache_tag = cfg->ccuSchd.binaryCacheTag;
    asccommCfg.ccu_ins = cfg->ccuIns;
    asccommCfg.stream = cfg->stream;
    asccommCfg.attrs = cfg->attrs;
    return asccomm_ccu_host_kernel_launch(kernel_func, &asccommCfg, args);
}

extern "C" uint64_t HcommCcuGetLaunchHashTag(const char* tag) { return asccomm_ccu_get_launch_hash_tag(tag); }
