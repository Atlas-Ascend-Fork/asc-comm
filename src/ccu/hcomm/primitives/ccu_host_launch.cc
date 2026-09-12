/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_host_launch.h"

#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "hcomm/common/ccu_device_context.h"
#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/kernel/ccu_kernel_registry_mgr.h"
#include "xxhash_impl.inc"

namespace {

constexpr uint64_t CCU_DIE0_MASK = 0x01U;
constexpr uint64_t CCU_DIE1_MASK = 0x02U;
constexpr uint32_t CCU_DIE1_ID = 1U;
constexpr uint32_t CCU_SUPPORTED_NUM_BLOCKS = 1U;

thread_local AsccommCcuHostLaunchAttrsV1 g_bound_attrs{};
thread_local bool g_has_bound_attrs = false;
thread_local CcuResult g_last_host_error = CcuResult::CCU_SUCCESS;

using VoidPackedCcuKernel = void (*)(void *);

struct VoidKernelRegisterCtx {
    const void *kernel_func;
    void *packed_args;
};

struct KernelCacheKey {
    int32_t device_logic_id;
    CcuInsHandle instance_handle;
    uint64_t generation;
    uint32_t die_id;
    const void *kernel_func;
    uint64_t binary_cache_tag;

    bool operator==(const KernelCacheKey &other) const
    {
        return device_logic_id == other.device_logic_id && instance_handle == other.instance_handle &&
            generation == other.generation && die_id == other.die_id && kernel_func == other.kernel_func &&
            binary_cache_tag == other.binary_cache_tag;
    }
};

struct KernelCacheKeyHash {
    size_t operator()(const KernelCacheKey &key) const
    {
        size_t value = std::hash<int32_t>{}(key.device_logic_id);
        const auto combine = [&value](size_t part) {
            value ^= part + static_cast<size_t>(0x9e3779b9U) + (value << 6U) + (value >> 2U);
        };
        combine(std::hash<CcuInsHandle>{}(key.instance_handle));
        combine(std::hash<uint64_t>{}(key.generation));
        combine(std::hash<uint32_t>{}(key.die_id));
        combine(std::hash<const void *>{}(key.kernel_func));
        combine(std::hash<uint64_t>{}(key.binary_cache_tag));
        return value;
    }
};

struct KernelCacheValue {
    ccu_kernel_handle kernel_handle;
    uint32_t task_args_num;
};

bool IsAbiHeaderValid(
    const HcommCcuAbiHeaderPod &header, uint32_t version, uint32_t magic_word, uint32_t expected_size)
{
    return header.version == version && header.magicWord == magic_word && header.size == expected_size &&
        header.reserved == 0;
}

bool IsRegisterContextShapeValid(const HcommCcuRegisterContextPod &context)
{
    return IsAbiHeaderValid(context.header, HCOMM_CCU_RES_ABI_VERSION,
               HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD, sizeof(context)) &&
        context.generation != 0 && context.deviceLogicId >= 0 && context.dieNum == HCOMM_CCU_MAX_DIE_NUM &&
        context.validDieMask != 0 &&
        (context.validDieMask & ~((1U << HCOMM_CCU_MAX_DIE_NUM) - 1U)) == 0 && context.instanceHandle != 0 &&
        context.reserved[0] == 0 && context.reserved[1] == 0;
}

bool IsLaunchContextShapeValid(const HcommCcuLaunchContextPod &context)
{
    return IsAbiHeaderValid(context.header, HCOMM_CCU_LAUNCH_ABI_VERSION,
               HCOMM_CCU_LAUNCH_CONTEXT_MAGIC_WORD, sizeof(context)) &&
        context.deviceLogicId >= 0 && context.runtimeStream != 0 && context.threadHandle != 0 &&
        context.reserved[0] == 0 && context.reserved[1] == 0;
}

CcuResult ValidateAttrs(const AsccommCcuHostLaunchAttrsV1 &attrs)
{
    if (!IsAbiHeaderValid(attrs.header, ASCCOMM_CCU_HOST_LAUNCH_ABI_VERSION,
            ASCCOMM_CCU_HOST_LAUNCH_ATTRS_MAGIC_WORD, sizeof(attrs)) ||
        attrs.registerContext == nullptr || attrs.generation == 0 || attrs.reserved[0] != 0 ||
        attrs.reserved[1] != 0) {
        return CcuResult::CCU_E_PARA;
    }

    const auto *register_context = reinterpret_cast<const HcommCcuRegisterContextPod *>(attrs.registerContext);
    if (!IsRegisterContextShapeValid(*register_context) || !IsLaunchContextShapeValid(attrs.launchContext) ||
        attrs.generation != register_context->generation ||
        attrs.launchContext.deviceLogicId != register_context->deviceLogicId) {
        return CcuResult::CCU_E_PARA;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ValidateLaunchCfg(const asccomm_launch_kernel_cfg &cfg, const AsccommCcuHostLaunchAttrsV1 &attrs)
{
    if (cfg.ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS || cfg.ccu_schd.reserved != 0 ||
        (cfg.ccu_schd.phy_die_mask != CCU_DIE0_MASK && cfg.ccu_schd.phy_die_mask != CCU_DIE1_MASK) ||
        cfg.ccu_ins == 0 || cfg.stream == nullptr) {
        return CcuResult::CCU_E_PARA;
    }

    const auto *register_context = reinterpret_cast<const HcommCcuRegisterContextPod *>(attrs.registerContext);
    const uint32_t die_id = cfg.ccu_schd.phy_die_mask == CCU_DIE1_MASK ? CCU_DIE1_ID : 0U;
    if (register_context->instanceHandle != cfg.ccu_ins ||
        (register_context->validDieMask & (1U << die_id)) == 0 ||
        attrs.launchContext.runtimeStream != reinterpret_cast<uint64_t>(cfg.stream)) {
        return CcuResult::CCU_E_PARA;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ResolveAttrs(const asccomm_launch_kernel_cfg &cfg, AsccommCcuHostLaunchAttrsV1 &attrs)
{
    if (cfg.attrs != nullptr) {
        attrs = *static_cast<const AsccommCcuHostLaunchAttrsV1 *>(cfg.attrs);
    } else if (g_has_bound_attrs) {
        attrs = g_bound_attrs;
    } else {
        return CcuResult::CCU_E_UNAVAIL;
    }
    CcuResult ret = ValidateAttrs(attrs);
    return ret == CcuResult::CCU_SUCCESS ? ValidateLaunchCfg(cfg, attrs) : ret;
}

CcuResult VoidKernelTrampoline(ccu_kernel_arg arg)
{
    auto *context = static_cast<VoidKernelRegisterCtx *>(arg);
    if (context == nullptr || context->kernel_func == nullptr || context->packed_args == nullptr) {
        return CcuResult::CCU_E_PTR;
    }
    auto kernel = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void *>(context->kernel_func));
    kernel(context->packed_args);
    return CcuResult::CCU_SUCCESS;
}

CcuResult RegisterKernel(const void *kernel_func, const asccomm_launch_kernel_cfg &cfg, void *args,
    const AsccommCcuHostLaunchAttrsV1 &attrs, ccu_kernel_handle &kernel_handle, uint32_t &task_args_num)
{
    CcuResult ret = asccomm_ccu_kernel_register_start(cfg.ccu_ins, attrs.registerContext);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }

    VoidKernelRegisterCtx register_context{kernel_func, args};
    const void *kernel_args[] = {&register_context};
    const uint32_t die_id = cfg.ccu_schd.phy_die_mask == CCU_DIE1_MASK ? CCU_DIE1_ID : 0U;
    ret = asccomm_ccu_kernel_register(cfg.ccu_ins, die_id, nullptr,
        reinterpret_cast<const void *>(VoidKernelTrampoline), kernel_args, 1, &kernel_handle);
    if (ret != CcuResult::CCU_SUCCESS) {
        (void)asccomm_ccu_kernel_register_end(cfg.ccu_ins);
        return ret;
    }

    ret = asccomm_ccu_kernel_register_end(cfg.ccu_ins);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }
    return asccomm_ccu_get_task_args_num(kernel_handle, &task_args_num);
}

class KernelHandleCache {
public:
    CcuResult Match(const void *kernel_func, const asccomm_launch_kernel_cfg &cfg, void *args,
        const AsccommCcuHostLaunchAttrsV1 &attrs, ccu_kernel_handle &kernel_handle, uint32_t &task_args_num)
    {
        const auto *register_context = reinterpret_cast<const HcommCcuRegisterContextPod *>(attrs.registerContext);
        const uint32_t die_id = cfg.ccu_schd.phy_die_mask == CCU_DIE1_MASK ? CCU_DIE1_ID : 0U;
        const KernelCacheKey key{register_context->deviceLogicId, cfg.ccu_ins, attrs.generation, die_id,
            kernel_func, cfg.ccu_schd.binary_cache_tag};

        // The lock also serializes the three-call registration transaction when caching is disabled.
        std::lock_guard<std::mutex> lock(mutex_);
        if (key.binary_cache_tag != 0) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                kernel_handle = it->second.kernel_handle;
                task_args_num = it->second.task_args_num;
                return CcuResult::CCU_SUCCESS;
            }
        }

        CcuResult ret = RegisterKernel(kernel_func, cfg, args, attrs, kernel_handle, task_args_num);
        if (ret != CcuResult::CCU_SUCCESS || key.binary_cache_tag == 0) {
            return ret;
        }
        try {
            cache_.emplace(key, KernelCacheValue{kernel_handle, task_args_num});
        } catch (...) {
            return CcuResult::CCU_E_INTERNAL;
        }
        return CcuResult::CCU_SUCCESS;
    }

    void Erase(int32_t device_logic_id, CcuInsHandle instance_handle, uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.device_logic_id == device_logic_id && it->first.instance_handle == instance_handle &&
                it->first.generation == generation) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    std::mutex mutex_{};
    std::unordered_map<KernelCacheKey, KernelCacheValue, KernelCacheKeyHash> cache_{};
};

KernelHandleCache &GetKernelHandleCache()
{
    static KernelHandleCache cache;
    return cache;
}

CcuResult SetLastError(CcuResult result)
{
    g_last_host_error = result;
    return result;
}

} // namespace

extern "C" CcuResult asccomm_ccu_host_launch_attrs_init(HcommCcuRegisterContextHandle register_context,
    const HcommCcuLaunchContextPod *launch_context, AsccommCcuHostLaunchAttrsV1 *attrs)
{
    if (register_context == nullptr || launch_context == nullptr || attrs == nullptr) {
        return SetLastError(CcuResult::CCU_E_PTR);
    }
    const auto *register_context_pod = reinterpret_cast<const HcommCcuRegisterContextPod *>(register_context);
    if (!IsRegisterContextShapeValid(*register_context_pod) || !IsLaunchContextShapeValid(*launch_context) ||
        register_context_pod->deviceLogicId != launch_context->deviceLogicId) {
        return SetLastError(CcuResult::CCU_E_PARA);
    }

    AsccommCcuHostLaunchAttrsV1 initialized{};
    initialized.header = {ASCCOMM_CCU_HOST_LAUNCH_ABI_VERSION,
        ASCCOMM_CCU_HOST_LAUNCH_ATTRS_MAGIC_WORD, sizeof(initialized), 0};
    initialized.registerContext = register_context;
    initialized.launchContext = *launch_context;
    initialized.generation = register_context_pod->generation;
    *attrs = initialized;
    return SetLastError(CcuResult::CCU_SUCCESS);
}

extern "C" CcuResult asccomm_ccu_host_context_bind(const AsccommCcuHostLaunchAttrsV1 *attrs)
{
    if (attrs == nullptr) {
        return SetLastError(CcuResult::CCU_E_PTR);
    }
    CcuResult ret = ValidateAttrs(*attrs);
    if (ret != CcuResult::CCU_SUCCESS) {
        return SetLastError(ret);
    }
    g_bound_attrs = *attrs;
    g_has_bound_attrs = true;
    return SetLastError(CcuResult::CCU_SUCCESS);
}

extern "C" void asccomm_ccu_host_context_unbind(void)
{
    g_bound_attrs = {};
    g_has_bound_attrs = false;
}

extern "C" CcuResult asccomm_ccu_host_get_last_error(void)
{
    return g_last_host_error;
}

extern "C" ccu_result asccomm_ccu_host_kernel_launch(
    const void *kernel_func, const asccomm_launch_kernel_cfg *cfg, void *args)
{
    try {
        if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
            return SetLastError(CcuResult::CCU_E_PTR);
        }

        AsccommCcuHostLaunchAttrsV1 attrs{};
        CcuResult ret = ResolveAttrs(*cfg, attrs);
        if (ret != CcuResult::CCU_SUCCESS) {
            return SetLastError(ret);
        }

        ccu_kernel_handle kernel_handle = 0;
        uint32_t task_args_num = 0;
        ret = GetKernelHandleCache().Match(kernel_func, *cfg, args, attrs, kernel_handle, task_args_num);
        if (ret != CcuResult::CCU_SUCCESS) {
            return SetLastError(ret);
        }
        return SetLastError(asccomm_ccu_kernel_launch(&attrs.launchContext, kernel_handle, args, task_args_num));
    } catch (...) {
        HCCL_ERROR("[%s] unexpected exception crossed the Host adapter boundary.", __func__);
        return SetLastError(CcuResult::CCU_E_INTERNAL);
    }
}

extern "C" CcuResult HcommCcuHostKernelLaunch(
    const void *kernel_func, const HcommLaunchKernelCfg *cfg, void *args)
{
    if (cfg == nullptr) {
        return asccomm_ccu_host_kernel_launch(kernel_func, nullptr, args);
    }
    asccomm_launch_kernel_cfg asccomm_cfg{};
    asccomm_cfg.ccu_schd.num_blocks = cfg->ccuSchd.numBlocks;
    asccomm_cfg.ccu_schd.reserved = cfg->ccuSchd.reserved;
    asccomm_cfg.ccu_schd.phy_die_mask = cfg->ccuSchd.phyDieMask;
    asccomm_cfg.ccu_schd.binary_cache_tag = cfg->ccuSchd.binaryCacheTag;
    asccomm_cfg.ccu_ins = cfg->ccuIns;
    asccomm_cfg.stream = cfg->stream;
    asccomm_cfg.attrs = cfg->attrs;
    return asccomm_ccu_host_kernel_launch(kernel_func, &asccomm_cfg, args);
}

extern "C" uint64_t asccomm_ccu_get_launch_hash_tag(const char *tag)
{
    return tag == nullptr ? 0 : static_cast<uint64_t>(XXH3_64bits(tag, std::strlen(tag)));
}

extern "C" uint64_t HcommCcuGetLaunchHashTag(const char *tag)
{
    return asccomm_ccu_get_launch_hash_tag(tag);
}

extern "C" CcuResult asccomm_ccu_instance_release(
    int32_t device_logic_id, CcuInsHandle ins_handle, uint64_t generation)
{
    if (device_logic_id < 0 || static_cast<uint32_t>(device_logic_id) >= asc::ccu_max_device_num ||
        ins_handle == 0 || generation == 0) {
        return CcuResult::CCU_E_PARA;
    }
    try {
        GetKernelHandleCache().Erase(device_logic_id, ins_handle, generation);
        return asc::ccu_kernel_registry_mgr::get_instance(device_logic_id).remove(ins_handle, generation);
    } catch (...) {
        HCCL_ERROR("[%s] unexpected exception crossed the Instance release boundary.", __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
}
