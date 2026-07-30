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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "rt_external_kernel.h"

#include "xxhash_impl.inc"

int32_t HcclGetThreadDeviceId();

#ifndef HCOMM_WEAK_SYMBOL
#define HCOMM_WEAK_SYMBOL __attribute__((weak))
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
constexpr uint32_t NOTIFY_DEFAULT_WAIT_TIME = 27U * 68U;
constexpr CcuResult CCU_E_RUNTIME_COMPAT = static_cast<CcuResult>(15);

bool IsLaunchLogEnabled()
{
    static const bool enabled = [] {
        const char* env = std::getenv("ASCCOMM_CCU_LAUNCH_LOG");
        return env != nullptr && std::strcmp(env, "0") != 0;
    }();
    return enabled;
}

#define ASCCOMM_CCU_LOG(fmt, ...)                                                       \
    do {                                                                                \
        if (IsLaunchLogEnabled()) {                                                     \
            std::fprintf(stderr, "[asccomm_ccu_host_launch] " fmt "\n", ##__VA_ARGS__); \
        }                                                                               \
    } while (0)

constexpr unsigned long long U64(uint64_t value) { return static_cast<unsigned long long>(value); }

constexpr int RetCode(CcuResult ret) { return static_cast<int>(ret); }

bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto getTaskArgsNum = HcommCcuGetTaskArgsNum;
    return registerStart != nullptr && registerKernel != nullptr && registerEnd != nullptr && getTaskArgsNum != nullptr;
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
    const int32_t deviceLogicId = HcclGetThreadDeviceId();
    ASCCOMM_CCU_LOG(
        "register start dev=%d kernel=%p ins=%llu die_mask=%llu tag=%llu args=%p", deviceLogicId, kernelFunc,
        U64(cfg->ccu_ins), U64(cfg->ccu_schd.phy_die_mask), U64(cfg->ccu_schd.binary_cache_tag), args);

    CcuResult ret = HcommCcuKernelRegisterStart(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG(
            "HcommCcuKernelRegisterStart failed dev=%d ins=%llu ret=%d", deviceLogicId, U64(cfg->ccu_ins),
            RetCode(ret));
        return ret;
    }

    const uint32_t dieId = static_cast<uint32_t>(cfg->ccu_schd.phy_die_mask);
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
        ASCCOMM_CCU_LOG(
            "HcommCcuKernelRegister failed dev=%d ins=%llu die=%u ret=%d", deviceLogicId, U64(cfg->ccu_ins), dieId,
            RetCode(ret));
        (void)HcommCcuKernelRegisterEnd(cfg->ccu_ins);
        return ret;
    }

    ret = HcommCcuKernelRegisterEnd(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG(
            "HcommCcuKernelRegisterEnd failed dev=%d ins=%llu handle=%llu ret=%d", deviceLogicId, U64(cfg->ccu_ins),
            U64(kernelHandle), RetCode(ret));
        return ret;
    }

    ASCCOMM_CCU_LOG(
        "register success dev=%d ins=%llu handle=%llu", deviceLogicId, U64(cfg->ccu_ins), U64(kernelHandle));
    return CCU_SUCCESS;
}

class KernelHandleCache {
public:
    CcuResult Match(
        const void* kernelFunc, const asccomm_launch_kernel_cfg* cfg, void* args, CcuKernelHandle& kernelHandle,
        uint32_t& taskArgsNum)
    {
        const uint64_t cacheTag = cfg->ccu_schd.binary_cache_tag;
        if (cacheTag == 0U) {
            ASCCOMM_CCU_LOG("cache disabled kernel=%p ins=%llu", kernelFunc, U64(cfg->ccu_ins));
            return RegisterAndGetTaskArgsNum(kernelFunc, cfg, args, kernelHandle, taskArgsNum);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cacheTag);
        if (iter != cache_.end()) {
            kernelHandle = iter->second;
            CcuResult ret = HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
            ASCCOMM_CCU_LOG(
                "cache hit tag=%llu handle=%llu task_args_num=%u ret=%d", U64(cacheTag), U64(kernelHandle), taskArgsNum,
                RetCode(ret));
            return ret;
        }

        ASCCOMM_CCU_LOG("cache miss tag=%llu kernel=%p ins=%llu", U64(cacheTag), kernelFunc, U64(cfg->ccu_ins));
        CcuResult ret = RegisterAndGetTaskArgsNum(kernelFunc, cfg, args, kernelHandle, taskArgsNum);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG(
                "cache miss register failed tag=%llu kernel=%p ins=%llu ret=%d", U64(cacheTag), kernelFunc,
                U64(cfg->ccu_ins), RetCode(ret));
            return ret;
        }
        cache_[cacheTag] = kernelHandle;
        ASCCOMM_CCU_LOG(
            "cache insert tag=%llu handle=%llu task_args_num=%u", U64(cacheTag), U64(kernelHandle), taskArgsNum);
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
            ASCCOMM_CCU_LOG("HcommCcuGetTaskArgsNum failed handle=%llu ret=%d", U64(kernelHandle), RetCode(ret));
            return ret;
        }
        ASCCOMM_CCU_LOG("task args num success handle=%llu task_args_num=%u", U64(kernelHandle), taskArgsNum);
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
            ASCCOMM_CCU_LOG("GetKernel failed dev=%u handle=%llu", devLogicId, U64(kernelHandle));
            return CCU_E_PTR;
        }

        std::vector<hcomm::CcuTaskParam> taskParams{};
        CcuResult ret = kernel->GeneTaskParams(static_cast<const uint64_t*>(taskArgs), argNum, taskParams);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG(
                "GeneTaskParams failed dev=%u handle=%llu arg_num=%u ret=%d", devLogicId, U64(kernelHandle), argNum,
                RetCode(ret));
            return ret;
        }
        if (taskParams.size() != 1) {
            ASCCOMM_CCU_LOG(
                "GeneTaskParams returned unsupported task count dev=%u handle=%llu count=%zu", devLogicId,
                U64(kernelHandle), taskParams.size());
            return CCU_E_NOT_SUPPORT;
        }

        taskParam = taskParams.front();
    } catch (...) {
        ASCCOMM_CCU_LOG("GetSingleTaskParam caught exception handle=%llu", U64(kernelHandle));
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
        ASCCOMM_CCU_LOG(
            "rtCCULaunch failed die=%u mission=%u inst_start=%u inst_cnt=%u key=%u arg_size=%u rt_ret=%d",
            taskInfo.dieId, taskInfo.missionId, taskInfo.instStartId, taskInfo.instCnt, taskInfo.key, taskInfo.argSize,
            rtRet);
        return CCU_E_RUNTIME_COMPAT;
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

extern "C" CcuResult asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const asccomm_launch_kernel_cfg* cfg, void* args)
{
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        ASCCOMM_CCU_LOG("launch invalid ptr kernel=%p cfg=%p args=%p", kernel_func, cfg, args);
        return CCU_E_PTR;
    }
    ASCCOMM_CCU_LOG(
        "launch enter dev=%d kernel=%p ins=%llu stream=%p die_mask=%llu tag=%llu args=%p", HcclGetThreadDeviceId(),
        kernel_func, U64(cfg->ccu_ins), cfg->stream, U64(cfg->ccu_schd.phy_die_mask),
        U64(cfg->ccu_schd.binary_cache_tag), args);
    if (!IsCcuKernelLaunchApiAvailable()) {
        ASCCOMM_CCU_LOG("launch api unavailable");
        return CCU_E_NOT_SUPPORT;
    }

    CcuKernelHandle kernelHandle = 0;
    uint32_t taskArgsNum = 0;
    CcuResult ret = GetKernelHandleCache().Match(kernel_func, cfg, args, kernelHandle, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG("launch match failed ret=%d", RetCode(ret));
        return ret;
    }

    hcomm::CcuTaskParam taskParam{};
    ret = GetSingleTaskParam(kernelHandle, args, taskArgsNum, taskParam);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG(
            "launch get task param failed handle=%llu task_args_num=%u ret=%d", U64(kernelHandle), taskArgsNum,
            RetCode(ret));
        return ret;
    }

    ret = LaunchSingleCcuTask(taskParam, cfg->stream);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG("launch task failed handle=%llu ret=%d", U64(kernelHandle), RetCode(ret));
        return ret;
    }
    ASCCOMM_CCU_LOG("launch success handle=%llu", U64(kernelHandle));
    return CCU_SUCCESS;
}
