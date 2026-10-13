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

#include "base/dlog_pub.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

#include "ccu/hcomm/ccu_launch.h"
#include "xxhash_impl.inc"

namespace {
constexpr uint64_t CCU_DIE0_MASK = 0x01U;
constexpr uint64_t CCU_DIE1_MASK = 0x02U;
constexpr uint32_t CCU_DIE0_ID = 0U;
constexpr uint32_t CCU_DIE1_ID = 1U;
constexpr uint32_t CCU_SUPPORTED_NUM_BLOCKS = 1U;
constexpr int32_t CCU_LOG_MODULE_ID = 5;

// Keep host launch behavior usable with test/minimal runtimes that omit the weak log backend.
#define ASCCOMM_CCU_LOG_ERROR(...)                      \
    do {                                                \
        if (DlogRecord != nullptr) {                    \
            dlog_error(CCU_LOG_MODULE_ID, __VA_ARGS__); \
        }                                               \
    } while (0)

bool IsSupportedSingleDieMask(uint64_t phyDieMask)
{
    return phyDieMask == CCU_DIE0_MASK || phyDieMask == CCU_DIE1_MASK;
}

uint32_t GetDieIdByMask(uint64_t phyDieMask) { return phyDieMask == CCU_DIE1_MASK ? CCU_DIE1_ID : CCU_DIE0_ID; }

CcuResult ValidateLaunchCfg(const asccomm_launch_kernel_cfg* cfg)
{
    if (cfg->ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported num_blocks[%u], expected[%u].", __func__, cfg->ccu_schd.num_blocks,
            CCU_SUPPORTED_NUM_BLOCKS);
        return CCU_E_PARA;
    }
    if (!IsSupportedSingleDieMask(cfg->ccu_schd.phy_die_mask)) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported phy_die_mask[0x%llx], expected[0x1 or 0x2].", __func__,
            static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask));
        return CCU_E_PARA;
    }
    return CCU_SUCCESS;
}

using VoidPackedCcuKernel = void (*)(void*);

struct VoidKernelRegisterCtx {
    const void* kernelFunc;
    void* packedArgs;
};

CcuResult VoidKernelTrampoline(ccu_kernel_arg arg);

CcuResult RegisterCcuKernel(
    const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
    ccu_kernel_handle& kernelHandle)
{
    CcuResult ret = asccomm_ccu_kernel_register_start(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] asccomm_ccu_kernel_register_start failed, ret[%d], ccuInsKey[0x%llx].", __func__, ret,
            static_cast<unsigned long long>(cfg->ccu_ins.ccuInsKey));
        return ret;
    }

    const uint32_t dieId = GetDieIdByMask(cfg->ccu_schd.phy_die_mask);
    VoidKernelRegisterCtx registerCtx{
        kernelFunc,
        args,
    };

    const void* kernelArgs[] = {&registerCtx};

    // kernelFunc do not support func that return void , only support return CcuResult
    // kernelName非空时透传给注册侧(profiling名称等), 为空时注册侧使用默认名称
    ret = asccomm_ccu_kernel_register(
        cfg->ccu_ins, dieId, kernelName, reinterpret_cast<const void*>(VoidKernelTrampoline), kernelArgs, 1,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR("[%s] asccomm_ccu_kernel_register failed, ret[%d], dieId[%u].", __func__, ret, dieId);
        (void)asccomm_ccu_kernel_register_end(cfg->ccu_ins);
        return ret;
    }

    ret = asccomm_ccu_kernel_register_end(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR("[%s] asccomm_ccu_kernel_register_end failed, ret[%d].", __func__, ret);
        return ret;
    }
    return CCU_SUCCESS;
}

class KernelHandleCache {
public:
    CcuResult Match(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        ccu_kernel_handle& kernelHandle, uint32_t& taskArgsNum)
    {
        const uint64_t cacheTag = cfg->ccu_schd.binary_cache_tag;
        if (cacheTag == 0U) {
            return RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cacheTag);
        if (iter != cache_.end()) {
            kernelHandle = iter->second;
            return asccomm_ccu_get_task_args_num(kernelHandle, &taskArgsNum);
        }

        CcuResult ret = RegisterAndGetTaskArgsNum(kernelFunc, kernelName, cfg, args, kernelHandle, taskArgsNum);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        cache_[cacheTag] = kernelHandle;
        return CCU_SUCCESS;
    }

private:
    CcuResult RegisterAndGetTaskArgsNum(
        const void* kernelFunc, const char* kernelName, const asccomm_launch_kernel_cfg* cfg, void* args,
        ccu_kernel_handle& kernelHandle, uint32_t& taskArgsNum)
    {
        CcuResult ret = RegisterCcuKernel(kernelFunc, kernelName, cfg, args, kernelHandle);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        ret = asccomm_ccu_get_task_args_num(kernelHandle, &taskArgsNum);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] asccomm_ccu_get_task_args_num failed, ret[%d], kernelHandle[0x%llx].", __func__, ret,
                static_cast<unsigned long long>(kernelHandle));
            return ret;
        }
        return CCU_SUCCESS;
    }

    std::mutex mutex_;
    std::unordered_map<uint64_t, ccu_kernel_handle> cache_;
};

KernelHandleCache& GetKernelHandleCache()
{
    static KernelHandleCache cache;
    return cache;
}

CcuResult VoidKernelTrampoline(ccu_kernel_arg arg)
{
    auto* ctx = static_cast<VoidKernelRegisterCtx*>(arg);
    if (ctx == nullptr || ctx->kernelFunc == nullptr || ctx->packedArgs == nullptr) {
        return CCU_E_PTR;
    }

    auto fn = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void*>(ctx->kernelFunc));
    fn(ctx->packedArgs);
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
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args)
{
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] invalid input, kernelFunc[%p], cfg[%p], args[%p].", __func__, kernel_func, cfg, args);
        return CCU_E_PTR;
    }
    CcuResult ret = ValidateLaunchCfg(cfg);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    ccu_kernel_handle kernelHandle = 0;
    uint32_t taskArgsNum = 0;
    ret = GetKernelHandleCache().Match(kernel_func, kernel_name, cfg, args, kernelHandle, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    ret = asccomm_ccu_kernel_launch(cfg->stream, kernelHandle, args, taskArgsNum);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] asccomm_ccu_kernel_launch failed, ret[%d], kernelHandle[0x%llx], stream[%p].", __func__, ret,
            static_cast<unsigned long long>(kernelHandle), cfg->stream);
        return ret;
    }
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuHostKernelLaunch(
    const void* kernel_func, const char* kernel_name, const HcommLaunchKernelCfg* cfg, void* args)
{
    if (cfg == nullptr) {
        return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, nullptr, args);
    }

    asccomm_launch_kernel_cfg asccommCfg{};
    asccommCfg.ccu_schd.num_blocks = cfg->ccuSchd.numBlocks;
    asccommCfg.ccu_schd.reserved = cfg->ccuSchd.reserved;
    asccommCfg.ccu_schd.phy_die_mask = cfg->ccuSchd.phyDieMask;
    asccommCfg.ccu_schd.binary_cache_tag = cfg->ccuSchd.binaryCacheTag;
    asccommCfg.ccu_ins = cfg->ccuIns;
    asccommCfg.stream = cfg->stream;
    asccommCfg.attrs = cfg->attrs;
    return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, &asccommCfg, args);
}

extern "C" uint64_t HcommCcuGetLaunchHashTag(const char* tag) { return asccomm_ccu_get_launch_hash_tag(tag); }
