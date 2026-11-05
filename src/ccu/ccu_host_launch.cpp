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
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ccu/hcomm/ccu_launch.h"
#include "hcomm/dfx/ccu_profiling.h"
#include "xxhash_impl.inc"

#include "hcomm/hcomm_res_defs.h"

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
extern CcuResult HcommCcuKernelLaunch(
    ThreadHandle threadHandle, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum) HCOMM_WEAK_SYMBOL;
extern HcommResult HcommThreadAllocWithStream(
    CommEngine engine, aclrtStream stream, uint32_t notifyNum, ThreadHandle* thread) HCOMM_WEAK_SYMBOL;
}

namespace {
constexpr uint64_t CCU_DIE0_MASK = 0x01U;
constexpr uint64_t CCU_DIE1_MASK = 0x02U;
constexpr uint32_t CCU_DIE0_ID = 0U;
constexpr uint32_t CCU_DIE1_ID = 1U;
constexpr uint32_t CCU_SUPPORTED_NUM_BLOCKS = 1U;
constexpr int32_t CCU_LOG_MODULE_ID = 5;
// 实例 token 与实体地址的判别上限，见 is_instance_entity_handle
constexpr uint64_t CCU_INS_TOKEN_MAX = 0xFFFFFFFFULL;

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

// token 走 hcomm 旧接口路径；实体地址走本仓数据面路径。
// 判别约定与 hcomm IsInstanceEntityQueryRequest 一致。
bool is_instance_entity_handle(const void* ccu_ins)
{
    return reinterpret_cast<std::uintptr_t>(ccu_ins) > CCU_INS_TOKEN_MAX;
}

// 实体 ABI 头校验，与 hcomm QueryInstanceEntity 对齐
bool is_valid_instance_entity(const HcommCcuInstanceEntity* entity)
{
    if (entity->header.version != HCOMM_CCU_INSTANCE_ENTITY_ABI_VERSION ||
        entity->header.magicWord != HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD ||
        entity->header.size != sizeof(HcommCcuInstanceEntity)) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] invalid ccu_ins entity, version[%u], magicWord[0x%x], size[%u], expected[size %zu].", __func__,
            entity->header.version, entity->header.magicWord, entity->header.size, sizeof(HcommCcuInstanceEntity));
        return false;
    }
    return true;
}

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

// --------------------------------------------------------------------------
// hcomm 旧接口路径（ccu_ins 为 CcuInsHandle token 的存量调用方）
// --------------------------------------------------------------------------

// SQE 任务的 uint64 槽位数
constexpr uint32_t CCU_SQE_ARGS_LEN = 13U;

bool is_hcomm_launch_api_available()
{
    return HcommCcuKernelRegisterStart != nullptr && HcommCcuKernelRegister != nullptr &&
           HcommCcuKernelRegisterEnd != nullptr && HcommCcuGetTaskArgsNum != nullptr &&
           HcommCcuKernelLaunch != nullptr && HcommThreadAllocWithStream != nullptr;
}

CcuResult convert_hcomm_result(HcommResult ret)
{
    switch (ret) {
        case 0:
            return CCU_SUCCESS;
        case 1:
            return CCU_E_PARA;
        case 2:
            return CCU_E_PTR;
        case 3:
            return CCU_E_INTERNAL;
        case 4:
            return CCU_E_INTERNAL;
        case 5:
            return CCU_E_NOT_SUPPORT;
        case 6:
            return CCU_E_NOT_FOUND;
        case 7:
            return CCU_E_UNAVAIL;
        default:
            return CCU_E_INTERNAL;
    }
}

CcuResult register_hcomm_kernel(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args,
    CcuKernelHandle& kernel_handle)
{
    CcuResult ret = HcommCcuKernelRegisterStart(reinterpret_cast<CcuInsHandle>(cfg->ccu_ins));
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelRegisterStart failed, ret[%d], ccu_ins[0x%llx].", __func__, ret,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(cfg->ccu_ins)));
        return ret;
    }

    const uint32_t die_id = get_die_id_by_mask(cfg->ccu_schd.phy_die_mask);
    VoidKernelRegisterCtx register_ctx{
        kernel_func,
        args,
    };

    const void* kernel_args[] = {&register_ctx};

    ret = HcommCcuKernelRegister(
        reinterpret_cast<CcuInsHandle>(cfg->ccu_ins), die_id, kernel_name,
        reinterpret_cast<const void*>(void_kernel_trampoline), kernel_args, 1, &kernel_handle);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR("[%s] HcommCcuKernelRegister failed, ret[%d], die_id[%u].", __func__, ret, die_id);
        (void)HcommCcuKernelRegisterEnd(reinterpret_cast<CcuInsHandle>(cfg->ccu_ins));
        return ret;
    }

    ret = HcommCcuKernelRegisterEnd(reinterpret_cast<CcuInsHandle>(cfg->ccu_ins));
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR("[%s] HcommCcuKernelRegisterEnd failed, ret[%d].", __func__, ret);
        return ret;
    }
    return CCU_SUCCESS;
}

// 缓存键为 (tag, token, die mask)：
// 9.2.0 仅用 binary_cache_tag，多实例同 tag 会跨实例复用句柄，此处修正
struct HcommKernelCacheKey {
    uint64_t binary_cache_tag;
    CcuInsHandle ccu_ins;
    uint64_t phy_die_mask;

    bool operator==(const HcommKernelCacheKey& other) const
    {
        return binary_cache_tag == other.binary_cache_tag && ccu_ins == other.ccu_ins &&
               phy_die_mask == other.phy_die_mask;
    }
};

struct HcommKernelCacheKeyHash {
    std::size_t operator()(const HcommKernelCacheKey& key) const
    {
        std::size_t hash = std::hash<uint64_t>{}(key.binary_cache_tag);
        hash ^= std::hash<uint64_t>{}(key.ccu_ins) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= std::hash<uint64_t>{}(key.phy_die_mask) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

class HcommKernelHandleCache {
public:
    CcuResult match(
        const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args,
        CcuKernelHandle& kernel_handle, uint32_t& task_args_num)
    {
        const uint64_t cache_tag = cfg->ccu_schd.binary_cache_tag;
        if (cache_tag == 0U) {
            return register_and_get_task_args_num(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
        }

        const HcommKernelCacheKey cache_key{
            cache_tag, reinterpret_cast<CcuInsHandle>(cfg->ccu_ins), cfg->ccu_schd.phy_die_mask};

        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(cache_key);
        if (iter != cache_.end()) {
            kernel_handle = iter->second;
            return HcommCcuGetTaskArgsNum(kernel_handle, &task_args_num);
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
        CcuKernelHandle& kernel_handle, uint32_t& task_args_num)
    {
        CcuResult ret = register_hcomm_kernel(kernel_func, kernel_name, cfg, args, kernel_handle);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        ret = HcommCcuGetTaskArgsNum(kernel_handle, &task_args_num);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] HcommCcuGetTaskArgsNum failed, ret[%d], kernel_handle[0x%llx].", __func__, ret,
                static_cast<unsigned long long>(kernel_handle));
            return ret;
        }
        return CCU_SUCCESS;
    }

    std::mutex mutex_;
    std::unordered_map<HcommKernelCacheKey, CcuKernelHandle, HcommKernelCacheKeyHash> cache_;
};

HcommKernelHandleCache& get_hcomm_kernel_handle_cache()
{
    static HcommKernelHandleCache cache;
    return cache;
}

// hcomm 旧路径以 ThreadHandle 为 launch 目标，需先按 stream 申请 CCU 引擎的通信线程
class ThreadHandleCache {
public:
    CcuResult get_or_create(aclrtStream stream, ThreadHandle& thread)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = cache_.find(stream);
        if (iter != cache_.end()) {
            thread = iter->second;
            return CCU_SUCCESS;
        }

        ThreadHandle new_thread = 0;
        const HcommResult alloc_ret = HcommThreadAllocWithStream(COMM_ENGINE_CCU, stream, 0, &new_thread);
        if (alloc_ret != 0) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] HcommThreadAllocWithStream failed, ret[%d], stream[%p].", __func__, alloc_ret, stream);
            return convert_hcomm_result(alloc_ret);
        }
        if (new_thread == 0) {
            ASCCOMM_CCU_LOG_ERROR("[%s] HcommThreadAllocWithStream returned an empty handle.", __func__);
            return CCU_E_INTERNAL;
        }

        cache_.emplace(stream, new_thread);
        thread = new_thread;
        return CCU_SUCCESS;
    }

private:
    std::mutex mutex_;
    std::unordered_map<aclrtStream, ThreadHandle> cache_;
};

ThreadHandleCache& get_thread_handle_cache()
{
    static ThreadHandleCache cache;
    return cache;
}

CcuResult launch_hcomm_kernel_with_stream(
    CcuKernelHandle kernel_handle, const void* task_args, uint32_t task_args_num, const asccomm_launch_kernel_cfg* cfg)
{
    const aclrtStream stream = cfg->stream;
    if (stream == nullptr) {
        ASCCOMM_CCU_LOG_ERROR("[%s] stream is nullptr.", __func__);
        return CCU_E_PTR;
    }
    if (task_args_num > CCU_SQE_ARGS_LEN) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] task argument count[%u] exceeds maximum[%u].", __func__, task_args_num, CCU_SQE_ARGS_LEN);
        return CCU_E_NOT_SUPPORT;
    }

    ThreadHandle thread_handle = 0;
    CcuResult ret = get_thread_handle_cache().get_or_create(stream, thread_handle);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    ret = HcommCcuKernelLaunch(thread_handle, kernel_handle, task_args, task_args_num);
    if (ret != CCU_SUCCESS) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] HcommCcuKernelLaunch failed, ret[%d], kernel_handle[0x%llx], stream[%p].", __func__, ret,
            static_cast<unsigned long long>(kernel_handle), stream);
        return ret;
    }
    return CCU_SUCCESS;
}

CcuResult launch_via_hcomm_legacy(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args)
{
    if (!is_hcomm_launch_api_available()) {
        ASCCOMM_CCU_LOG_ERROR(
            "[%s] hcomm legacy launch api unavailable, ccu_ins[0x%llx].", __func__,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(cfg->ccu_ins)));
        return CCU_E_NOT_SUPPORT;
    }

    CcuKernelHandle kernel_handle = 0;
    uint32_t task_args_num = 0;
    CcuResult ret =
        get_hcomm_kernel_handle_cache().match(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    return launch_hcomm_kernel_with_stream(kernel_handle, args, task_args_num, cfg);
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

    if (is_instance_entity_handle(cfg->ccu_ins)) {
        // 新数据面路径：ccu_ins 为 hcomm 持有的 HcommCcuInstanceEntity 稳定地址
        const auto* ccu_instance = static_cast<const HcommCcuInstanceEntity*>(cfg->ccu_ins);
        if (!is_valid_instance_entity(ccu_instance)) {
            return CCU_E_PARA;
        }

        ccu_kernel_handle kernel_handle = 0;
        uint32_t task_args_num = 0;
        ret = get_kernel_handle_cache().match(kernel_func, kernel_name, cfg, args, kernel_handle, task_args_num);
        if (ret != CCU_SUCCESS) {
            return ret;
        }

        // 数据面 profiling 上下文（上报失败只记日志，不影响 launch）
        asc::ccu_profiling::ccu_prof_report_context prof_ctx{};
        prof_ctx.kernel_func = kernel_func;
        if (kernel_name != nullptr) {
            (void)snprintf(prof_ctx.kernel_name, sizeof(prof_ctx.kernel_name), "%s", kernel_name);
        }
        prof_ctx.die_id = get_die_id_by_mask(cfg->ccu_schd.phy_die_mask);
        const bool prof_enabled = asc::ccu_profiling::is_ccu_prof_report_ready();
        if (prof_enabled) {
            prof_ctx.begin_time = asc::ccu_profiling::msprof_sys_cycle_time();
        }

        ret = asccomm_ccu_kernel_launch(cfg->stream, kernel_handle, args, task_args_num);
        if (ret != CCU_SUCCESS) {
            ASCCOMM_CCU_LOG_ERROR(
                "[%s] asccomm_ccu_kernel_launch failed, ret[%d], kernel_handle[0x%llx], stream[%p].", __func__, ret,
                static_cast<unsigned long long>(kernel_handle), cfg->stream);
            return ret;
        }

        if (prof_enabled) {
            prof_ctx.end_time = asc::ccu_profiling::msprof_sys_cycle_time();
            prof_ctx.task_info_valid =
                asc::ccu_profiling::query_ccu_task_id_and_stream_id(prof_ctx.task_id, prof_ctx.stream_id);
            std::vector<asc::ccu_profiling::profiling_detail> profiling_details;
            const CcuResult detail_ret =
                asc::ccu_profiling::get_ccu_profiling_details(kernel_handle, args, task_args_num, profiling_details);
            if (detail_ret != CCU_SUCCESS) {
                ASCCOMM_CCU_LOG_ERROR(
                    "[%s] get_ccu_profiling_details failed, ret[%d], profiling report skipped.", __func__, detail_ret);
            } else {
                asc::ccu_profiling::report_ccu_data_plane_after_launch(prof_ctx, profiling_details);
            }
        }
        return CCU_SUCCESS;
    }

    // hcomm 旧接口路径
    return launch_via_hcomm_legacy(kernel_func, kernel_name, cfg, args);
}

extern "C" CcuResult HcommCcuHostKernelLaunch(
    const void* kernel_func, const char* kernel_name, const HcommLaunchKernelCfg* cfg, void* args)
{
    if (cfg == nullptr) {
        return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, nullptr, args);
    }

    asccomm_launch_kernel_cfg asccomm_cfg{};
    asccomm_cfg.ccu_schd.num_blocks = cfg->ccuSchd.numBlocks;
    asccomm_cfg.ccu_schd.reserved = cfg->ccuSchd.reserved;
    asccomm_cfg.ccu_schd.phy_die_mask = cfg->ccuSchd.phyDieMask;
    asccomm_cfg.ccu_schd.binary_cache_tag = cfg->ccuSchd.binaryCacheTag;
    asccomm_cfg.ccu_ins = reinterpret_cast<void*>(cfg->ccuIns); // 保持 token 位型
    asccomm_cfg.stream = cfg->stream;
    asccomm_cfg.attrs = cfg->attrs;
    return asccomm_ccu_host_kernel_launch(kernel_func, kernel_name, &asccomm_cfg, args);
}

extern "C" uint64_t HcommCcuGetLaunchHashTag(const char* tag) { return asccomm_ccu_get_launch_hash_tag(tag); }
