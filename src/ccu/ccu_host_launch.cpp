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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ccu/hcomm/ccu_launch.h"
#include "ccu_profiling.h"
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

bool is_supported_single_die_mask(uint64_t phy_die_mask)
{
    return phy_die_mask == CCU_DIE0_MASK || phy_die_mask == CCU_DIE1_MASK;
}

uint32_t get_die_id_by_mask(uint64_t phy_die_mask) { return phy_die_mask == CCU_DIE1_MASK ? CCU_DIE1_ID : CCU_DIE0_ID; }

CcuResult validate_launch_cfg(const asccomm_launch_kernel_cfg* cfg)
{
    if (cfg->ccu_schd.num_blocks != CCU_SUPPORTED_NUM_BLOCKS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported num_blocks[%u], expected[%u].", __func__, cfg->ccu_schd.num_blocks,
            CCU_SUPPORTED_NUM_BLOCKS);
        return CCU_E_PARA;
    }
    if (!is_supported_single_die_mask(cfg->ccu_schd.phy_die_mask)) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] unsupported phy_die_mask[0x%llx], expected[0x1 or 0x2].", __func__,
            static_cast<unsigned long long>(cfg->ccu_schd.phy_die_mask));
        return CCU_E_PARA;
    }
    return CCU_SUCCESS;
}

using VoidPackedCcuKernel = void (*)(void*);

struct VoidKernelRegisterCtx {
    const void* kernel_func;
    void* packed_args;
};

CcuResult void_kernel_trampoline(ccu_kernel_arg arg);

CcuResult register_ccu_kernel(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args,
    ccu_kernel_handle& kernel_handle)
{
    CcuResult ret = asccomm_ccu_kernel_register_start(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] asccomm_ccu_kernel_register_start failed, ret[%d], ccu_ins[%p].", __func__, ret, cfg->ccu_ins);
        return ret;
    }

    const uint32_t die_id = get_die_id_by_mask(cfg->ccu_schd.phy_die_mask);
    VoidKernelRegisterCtx register_ctx{
        kernel_func,
        args,
    };

    const void* kernel_args[] = {&register_ctx};

    // kernel_func do not support func that return void , only support return CcuResult
    // kernel_name非空时透传给注册侧(profiling名称等), 为空时注册侧使用默认名称
    ret = asccomm_ccu_kernel_register(
        cfg->ccu_ins, die_id, kernel_name, reinterpret_cast<const void*>(void_kernel_trampoline), kernel_args, 1,
        &kernel_handle);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR("[%s] asccomm_ccu_kernel_register failed, ret[%d], die_id[%u].", __func__, ret, die_id);
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

struct CcuInstanceCacheIdentity {
    uint64_t key;
    std::uintptr_t instance;

    bool operator==(const CcuInstanceCacheIdentity& other) const
    {
        return key == other.key && instance == other.instance;
    }
};

struct KernelCacheKey {
    uint64_t binary_cache_tag;
    CcuInstanceCacheIdentity instance;
    uint64_t phy_die_mask;

    bool operator==(const KernelCacheKey& other) const
    {
        return binary_cache_tag == other.binary_cache_tag && instance == other.instance &&
               phy_die_mask == other.phy_die_mask;
    }
};

struct KernelCacheKeyHash {
    std::size_t operator()(const KernelCacheKey& key) const
    {
        std::size_t hash = std::hash<uint64_t>{}(key.binary_cache_tag);
        hash ^= std::hash<uint64_t>{}(key.instance.key) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<std::uintptr_t>{}(key.instance.instance) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<uint64_t>{}(key.phy_die_mask) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

class KernelHandleCache {
public:
    CcuResult match(
        const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args,
        ccu_kernel_handle& kernel_handle, uint32_t& task_args_num)
    {
        const uint64_t cache_tag = cfg->ccu_schd.binary_cache_tag;
        if (cache_tag == 0U) {
            return register_and_get_task_args_num(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
        }
        if (cfg->ccu_ins == nullptr) {
            ASCCOMM_CCU_LOG_ERROR("[%s] ccu_ins is null while binary cache is enabled.", __func__);
            return CCU_E_PTR;
        }

        const auto* ccu_instance = static_cast<const HcommCcuInstanceEntity*>(cfg->ccu_ins);
        const KernelCacheKey cache_key{
            cache_tag,
            {ccu_instance->ccuInsKey, reinterpret_cast<std::uintptr_t>(ccu_instance)},
            cfg->ccu_schd.phy_die_mask};

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cache_key);
        if (iter != cache_.end()) {
            kernel_handle = iter->second;
            return asccomm_ccu_get_task_args_num(kernel_handle, &task_args_num);
        }

        CcuResult ret =
            register_and_get_task_args_num(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        cache_[cache_key] = kernel_handle;
        return CCU_SUCCESS;
    }

private:
    CcuResult register_and_get_task_args_num(
        const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args,
        ccu_kernel_handle& kernel_handle, uint32_t& task_args_num)
    {
        CcuResult ret = register_ccu_kernel(kernel_func, kernel_name, cfg, args, kernel_handle);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        ret = asccomm_ccu_get_task_args_num(kernel_handle, &task_args_num);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] asccomm_ccu_get_task_args_num failed, ret[%d], kernel_handle[0x%llx].", __func__, ret,
                static_cast<unsigned long long>(kernel_handle));
            return ret;
        }
        return CCU_SUCCESS;
    }

    std::mutex mutex_;
    std::unordered_map<KernelCacheKey, ccu_kernel_handle, KernelCacheKeyHash> cache_;
};

KernelHandleCache& get_kernel_handle_cache()
{
    static KernelHandleCache cache;
    return cache;
}

CcuResult void_kernel_trampoline(ccu_kernel_arg arg)
{
    auto* ctx = static_cast<VoidKernelRegisterCtx*>(arg);
    if (ctx == nullptr || ctx->kernel_func == nullptr || ctx->packed_args == nullptr) {
        return CCU_E_PTR;
    }

    auto fn = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void*>(ctx->kernel_func));
    fn(ctx->packed_args);
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
            "[%s] invalid input, kernel_func[%p], cfg[%p], args[%p].", __func__, kernel_func, cfg, args);
        return CCU_E_PTR;
    }
    CcuResult ret = validate_launch_cfg(cfg);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    ccu_kernel_handle kernel_handle = 0;
    uint32_t task_args_num = 0;
    ret = get_kernel_handle_cache().match(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    (void)asc::ccu_profiling::init_ccu_profiling_once();
    asc::ccu_profiling::ccu_prof_report_context profCtx{};
    profCtx.kernel_func = kernel_func;
    profCtx.kernel_name = kernel_name;
    profCtx.die_id = get_die_id_by_mask(cfg->ccu_schd.phy_die_mask);
    const bool profEnabled = asc::ccu_profiling::is_ccu_prof_report_ready();
    if (profEnabled) {
        profCtx.begin_time = asc::ccu_profiling::msprof_sys_cycle_time();
    }

    ret = asccomm_ccu_kernel_launch(cfg->stream, kernel_handle, args, task_args_num);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] asccomm_ccu_kernel_launch failed, ret[%d], kernel_handle[0x%llx], stream[%p].", __func__, ret,
            static_cast<unsigned long long>(kernel_handle), cfg->stream);
        return ret;
    }

    if (profEnabled) {
        profCtx.end_time = asc::ccu_profiling::msprof_sys_cycle_time();
        std::vector<asc::ccu_profiling::profiling_detail> profiling_details;
        const CcuResult detail_ret = asc::ccu_profiling::get_ccu_profiling_details(
            kernel_handle, args, task_args_num, profiling_details);
        if (detail_ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] get_ccu_profiling_details failed, ret[%d], profiling report skipped.", __func__,
                detail_ret);
        } else {
            asc::ccu_profiling::report_ccu_data_plane_after_launch(profCtx, profiling_details);
        }
    }
    return CCU_SUCCESS;
}
