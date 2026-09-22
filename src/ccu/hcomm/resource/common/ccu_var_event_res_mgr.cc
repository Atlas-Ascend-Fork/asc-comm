/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/common/ccu_var_event_res_mgr.h"

#include <mutex>

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_common.h"
#include "hcomm/common/ccu_device_context.h"

#include "rt_external.h"

namespace asc {

ccu_var_event_res_mgr& ccu_var_event_res_mgr::get_instance(const int32_t device_logic_id)
{
    static ccu_var_event_res_mgr res_mgrs[ccu_max_device_num + 1];

    int32_t dev_logic_id = device_logic_id;
    if (dev_logic_id < 0 || static_cast<uint32_t>(dev_logic_id) >= ccu_max_device_num) {
        HCCL_WARNING(
            "[CcuVarEventResMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.",
            __func__, dev_logic_id, ccu_max_device_num);
        dev_logic_id = ccu_max_device_num;
    }

    res_mgrs[dev_logic_id].dev_logic_id_ = dev_logic_id;
    return res_mgrs[dev_logic_id];
}

CcuResult ccu_var_event_res_mgr::alloc_from_pool(
    std::vector<HcommCcuResRangePod>& pool, uint32_t num, std::vector<HcommCcuResRangePod>& out)
{
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if (it->count < num) {
            continue;
        }

        out.clear();
        out.emplace_back(HcommCcuResRangePod{it->resourceType, it->dieId, it->startId, num});

        if (it->count == num) {
            pool.erase(it);
        } else {
            it->startId += num;
            it->count -= num;
        }
        return CCU_SUCCESS;
    }

    return CCU_E_UNAVAIL;
}

void ccu_var_event_res_mgr::return_to_pool(
    std::vector<HcommCcuResRangePod>& pool, const std::vector<HcommCcuResRangePod>& ranges)
{
    for (const auto& range : ranges) {
        if (range.count == 0) {
            continue;
        }
        pool.emplace_back(range);
    }
}

static std::vector<HcommCcuResRangePod>* select_pool(
    asc_ccu_res_repository& res_repo, ccu_var_event_type type, uint8_t die_id)
{
    switch (type) {
        case ccu_var_event_type::variable:
            return &res_repo.block_xn[die_id];
        case ccu_var_event_type::event:
            return &res_repo.block_cke[die_id];
        default:
            return nullptr;
    }
}

// 由预约资源类型推导runtime资源类型，不支持的类型返回false
static bool get_rt_res_type(ccu_var_event_type type, rtDevResType_t& res_type)
{
    switch (type) {
        case ccu_var_event_type::variable:
            res_type = RT_RES_TYPE_CCU_XN;
            return true;
        case ccu_var_event_type::event:
            res_type = RT_RES_TYPE_CCU_CKE;
            return true;
        default:
            return false;
    }
}

static CcuResult map_dev_res_address(uint8_t die_id, rtDevResType_t res_type, uint32_t res_id, uint64_t& va)
{
    rtDevResInfo res_info{};
    res_info.dieId = die_id;
    res_info.procType = RT_PROCESS_CP1;
    res_info.resType = res_type;
    res_info.resId = res_id;
    res_info.flag = 0;

    uint64_t mapped_addr = 0;
    uint32_t mapped_len = 0;
    rtDevResAddrInfo addr_info{};
    addr_info.resAddress = &mapped_addr;
    addr_info.len = &mapped_len;

    rtError_t ret = rtGetDevResAddress(&res_info, &addr_info);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] rtGetDevResAddress failed[%d], die_id[%u] res_type[%d] "
            "res_id[%u].",
            __func__, ret, die_id, static_cast<int32_t>(res_type), res_id);
        return CCU_E_RUNTIME;
    }

    va = mapped_addr;
    return CCU_SUCCESS;
}

// 解除map_dev_res_address映射的进程可访问VA，与映射一一对应
static CcuResult unmap_dev_res_address(uint8_t die_id, rtDevResType_t res_type, uint32_t res_id)
{
    rtDevResInfo res_info{};
    res_info.dieId = die_id;
    res_info.procType = RT_PROCESS_CP1;
    res_info.resType = res_type;
    res_info.resId = res_id;
    res_info.flag = 0;

    rtError_t ret = rtReleaseDevResAddress(&res_info);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] rtReleaseDevResAddress failed[%d], die_id[%u] "
            "res_type[%d] res_id[%u].",
            __func__, ret, die_id, static_cast<int32_t>(res_type), res_id);
        return CCU_E_RUNTIME;
    }

    return CCU_SUCCESS;
}

namespace {
// RegisterAddrs 中记录本次已成功映射的资源，供失败回滚逆序解除
struct mapped_res {
    uint8_t die_id;
    uint32_t res_id;
};
} // namespace

// 逆序解除本次已完成的映射，与 map_dev_res_address 一一对应
static void unmap_mapped_res(
    const std::vector<mapped_res>& mapped, rtDevResType_t res_type, uint64_t handle, ccu_var_event_type type)
{
    HCCL_RUN_WARNING(
        "[CcuVarEventResMgr][%s] rollback, unmap [%zu] mapped res of "
        "handle[0x%llx] type[%d].",
        __func__, mapped.size(), handle, static_cast<int32_t>(type));
    for (auto it = mapped.rbegin(); it != mapped.rend(); ++it) {
        (void)unmap_dev_res_address(it->die_id, res_type, it->res_id);
    }
}

CcuResult ccu_var_event_res_mgr::register_addrs(ccu_var_event_type type, uint64_t handle, uint32_t num)
{
    rtDevResType_t res_type = RT_RES_TYPE_CCU_XN;
    if (!get_rt_res_type(type, res_type)) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, unsupported type[%d], handle[0x%llx].", __func__,
            static_cast<int32_t>(type), handle);
        return CCU_E_PARA;
    }

    std::vector<uint64_t> va_list{};
    std::vector<mapped_res> mapped{};
    va_list.reserve(num);
    mapped.reserve(num);

    // 归一本函数内三处失败回滚路径：均为“逆序解除已完成映射”，收敛成一个闭包，
    // 避免 unmap_mapped_res 调用点重复三份；(void) 消歧义式丢弃返回值，规避静态检查误报
    auto rollback = [&mapped, res_type, handle, type]() { (void)unmap_mapped_res(mapped, res_type, handle, type); };

    for (uint32_t index = 0; index < num; index++) {
        uint8_t die_id = 0;
        uint32_t res_id = 0;
        CcuResult id_ret = (type == ccu_var_event_type::variable) ? get_variable_xn_id(handle, index, die_id, res_id) :
                                                                    get_event_cke_id(handle, index, die_id, res_id);
        if (id_ret != CCU_SUCCESS) {
            rollback();
            return id_ret;
        }

        uint64_t va = 0;
        CcuResult map_ret = map_dev_res_address(die_id, res_type, res_id, va);
        if (map_ret != CCU_SUCCESS) {
            rollback();
            return map_ret;
        }
        va_list.push_back(va);
        mapped.push_back({die_id, res_id});
    }

    CcuResult save_ret = save_addrs(type, handle, va_list);
    if (save_ret != CCU_SUCCESS) {
        rollback();
        return save_ret;
    }
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::alloc_and_record(
    CcuInsHandle ins_handle, asc_ccu_res_repository& res_repo, ccu_var_event_type type, uint8_t die_id, uint32_t num,
    uint64_t& new_handle)
{
    if (die_id >= HCOMM_CCU_MAX_DIE_NUM) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, die_id[%u] should be less than %u.", __func__, die_id,
            HCOMM_CCU_MAX_DIE_NUM);
        return CCU_E_PARA;
    }
    if (num == 0) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] failed, num should not be 0.", __func__);
        return CCU_E_PARA;
    }

    std::vector<HcommCcuResRangePod>* pool = select_pool(res_repo, type, die_id);
    if (pool == nullptr) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] failed, unsupported type[%d].", __func__, static_cast<int32_t>(type));
        return CCU_E_PARA;
    }

    std::vector<HcommCcuResRangePod> res_ranges{};
    CcuResult ret = alloc_from_pool(*pool, num, res_ranges);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, no consecutive block of num[%u] in ins_handle[0x%llx] "
            "resource pool, die_id[%u] type[%d].",
            __func__, num, ins_handle, die_id, static_cast<int32_t>(type));
        return ret;
    }

    ccu_var_event_res res{};
    res.ins_handle = ins_handle;
    res.dev_logic_id = dev_logic_id_;
    res.die_id = die_id;
    res.type = type;
    res.res_ranges = std::move(res_ranges);
    res.res_repo = &res_repo;

    std::unique_lock<std::shared_timed_mutex> lock(map_mutex_);
    handle_seed_ += 1;
    new_handle = handle_seed_;
    res_map_.emplace(new_handle, std::move(res));
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::acquire(
    CcuInsHandle ins_handle, asc_ccu_res_repository& res_repo, ccu_var_event_type type, uint8_t die_id, uint32_t num,
    uint64_t& handle)
{
    uint64_t new_handle = 0;
    CcuResult alloc_ret = alloc_and_record(ins_handle, res_repo, type, die_id, num, new_handle);
    if (alloc_ret != CCU_SUCCESS) {
        return alloc_ret;
    }

    // 申请期即完成地址映射；失败与切池动作配对，整笔撤销后不写出参
    CcuResult reg_ret = register_addrs(type, new_handle, num);
    if (reg_ret != CCU_SUCCESS) {
        HCCL_RUN_WARNING(
            "[CcuVarEventResMgr][%s] register addrs failed[%d], release acquired "
            "handle[0x%llx] ins_handle[0x%llx] die_id[%u] type[%d] num[%u].",
            __func__, static_cast<int32_t>(reg_ret), new_handle, ins_handle, die_id, static_cast<int32_t>(type), num);
        (void)release_by_handle(new_handle);
        return reg_ret;
    }

    handle = new_handle;
    HCCL_RUN_INFO(
        "[CcuVarEventResMgr][%s] success, devLogicId[%d] ins_handle[0x%llx] die_id[%u] "
        "type[%d] num[%u] handle[0x%llx].",
        __func__, dev_logic_id_, ins_handle, die_id, static_cast<int32_t>(type), num, handle);
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::get_variable_xn_id(
    uint64_t handle, uint32_t index, uint8_t& die_id, uint32_t& xn_id) const
{
    std::shared_lock<std::shared_timed_mutex> lock(map_mutex_);
    auto it = res_map_.find(handle);
    if (it == res_map_.end()) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] handle[0x%llx] is not existed.", __func__, handle);
        return CCU_E_NOT_FOUND;
    }

    const auto& res = it->second;
    if (res.type != ccu_var_event_type::variable) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] failed, handle[0x%llx] is not a variable(xn) resource.", __func__, handle);
        return CCU_E_PARA;
    }
    if (res.res_ranges.size() != 1) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] get variable resource id failed, variable resource is fragmented into %zu blocks.",
            __func__, res.res_ranges.size());
        return CCU_E_NOT_SUPPORT;
    }

    const HcommCcuResRangePod& range = res.res_ranges[0];
    if (index >= range.count) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] get variable resource id failed, index[%u] out of range, block count[%u].",
            __func__, index, range.count);
        return CCU_E_PARA;
    }

    die_id = res.die_id;
    xn_id = range.startId + index;
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::get_event_cke_id(
    uint64_t handle, uint32_t index, uint8_t& die_id, uint32_t& cke_id) const
{
    std::shared_lock<std::shared_timed_mutex> lock(map_mutex_);
    auto it = res_map_.find(handle);
    if (it == res_map_.end()) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] handle[0x%llx] is not existed.", __func__, handle);
        return CCU_E_NOT_FOUND;
    }

    const auto& res = it->second;
    if (res.type != ccu_var_event_type::event) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] get event resource id failed, handle[0x%llx] is not an event(cke) resource.",
            __func__, handle);
        return CCU_E_PARA;
    }
    if (res.res_ranges.size() != 1) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] get event resource id failed, event resource is fragmented into %zu blocks.",
            __func__, res.res_ranges.size());
        return CCU_E_NOT_SUPPORT;
    }

    const HcommCcuResRangePod& range = res.res_ranges[0];
    if (index >= range.count) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, index[%u] out of range, block count[%u].", __func__, index, range.count);
        return CCU_E_PARA;
    }

    die_id = res.die_id;
    cke_id = range.startId + index;
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::save_addrs(
    ccu_var_event_type type, uint64_t handle, const std::vector<uint64_t>& va_list)
{
    std::unique_lock<std::shared_timed_mutex> lock(map_mutex_);
    auto it = res_map_.find(handle);
    if (it == res_map_.end()) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] handle[0x%llx] is not existed.", __func__, handle);
        return CCU_E_NOT_FOUND;
    }

    auto& res = it->second;
    if (res.type != type) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, handle[0x%llx] type mismatch, expect[%d] "
            "actual[%d].",
            __func__, handle, static_cast<int32_t>(type), static_cast<int32_t>(res.type));
        return CCU_E_PARA;
    }
    if (res.res_ranges.size() != 1) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, resource is fragmented into %zu blocks.", __func__, res.res_ranges.size());
        return CCU_E_NOT_SUPPORT;
    }
    if (va_list.size() != res.res_ranges[0].count) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, va count[%zu] mismatch resource count[%u].", __func__, va_list.size(),
            res.res_ranges[0].count);
        return CCU_E_PARA;
    }

    res.va_list = va_list;
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::get_saved_addr(
    ccu_var_event_type type, uint64_t handle, uint32_t index, uint64_t& va) const
{
    std::shared_lock<std::shared_timed_mutex> lock(map_mutex_);
    auto it = res_map_.find(handle);
    if (it == res_map_.end()) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] handle[0x%llx] is not existed.", __func__, handle);
        return CCU_E_NOT_FOUND;
    }

    const auto& res = it->second;
    if (res.type != type) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, handle[0x%llx] type mismatch, expect[%d] "
            "actual[%d].",
            __func__, handle, static_cast<int32_t>(type), static_cast<int32_t>(res.type));
        return CCU_E_PARA;
    }
    if (index >= res.va_list.size()) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed, index[%u] out of range, registered num[%zu].", __func__, index,
            res.va_list.size());
        return CCU_E_PARA;
    }

    va = res.va_list[index];
    return CCU_SUCCESS;
}

CcuResult ccu_var_event_res_mgr::unmap_saved_addrs(const ccu_var_event_res& res)
{
    // 仅 unmap Alloc 阶段已成功映射并保存 VA 的资源；错误回滚路径中 va_list 为空，天然跳过。
    // 不变量：va_list 非空 <=> SaveAddrs 已成功，而 SaveAddrs 强校验 res_ranges 为单个连续块且
    // va_list.size() == res_ranges[0].count，故下面用 res_ranges[0].startId + index 反推 res_id 恒成立
    if (res.va_list.empty() || res.res_ranges.empty()) {
        return CCU_SUCCESS;
    }
    // 上述不变量当前由 SaveAddrs 保证，此处再作一次防御校验：一旦将来分配策略改为可返回多块，
    // 用首块 start_id 反推 res_id 会越出块边界，宁可跳过 unmap 也不能解除错误资源的映射
    if (res.res_ranges.size() != 1) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] unexpected fragmented res_ranges size[%zu], skip unmap.", __func__,
            res.res_ranges.size());
        return CCU_E_NOT_SUPPORT;
    }

    rtDevResType_t res_type = RT_RES_TYPE_CCU_XN;
    if (!get_rt_res_type(res.type, res_type)) {
        HCCL_ERROR("[CcuVarEventResMgr][%s] failed, unsupported type[%d].", __func__, static_cast<int32_t>(res.type));
        return CCU_E_PARA;
    }

    CcuResult first_err = CCU_SUCCESS;
    const uint32_t start_id = res.res_ranges[0].startId;
    for (uint32_t index = 0; index < res.va_list.size(); index++) {
        CcuResult ret = unmap_dev_res_address(res.die_id, res_type, start_id + index);
        if (ret != CCU_SUCCESS && first_err == CCU_SUCCESS) {
            first_err = ret;
        }
    }
    return first_err;
}

CcuResult ccu_var_event_res_mgr::release_by_handle(uint64_t handle)
{
    ccu_var_event_res res{};
    {
        std::unique_lock<std::shared_timed_mutex> lock(map_mutex_);
        auto it = res_map_.find(handle);
        if (it == res_map_.end()) {
            HCCL_ERROR("[CcuVarEventResMgr][%s] handle[0x%llx] is not existed.", __func__, handle);
            return CCU_E_NOT_FOUND;
        }
        res = std::move(it->second);
        res_map_.erase(it);
    }

    // 归还资源池前，先解除该 handle 已映射的进程可访问 VA；
    // unmap 失败不阻断归还，错误码上抛由调用方决定是否处理
    CcuResult unmap_ret = unmap_saved_addrs(res);
    if (unmap_ret != CCU_SUCCESS) {
        HCCL_RUN_WARNING(
            "[CcuVarEventResMgr][%s] unmap failed[%d], continue to return resources, "
            "handle[0x%llx].",
            __func__, static_cast<int32_t>(unmap_ret), handle);
    }

    if (res.res_repo == nullptr) {
        return unmap_ret;
    }
    std::vector<HcommCcuResRangePod>* pool = select_pool(*res.res_repo, res.type, res.die_id);
    if (pool == nullptr) {
        HCCL_ERROR(
            "[CcuVarEventResMgr][%s] failed to return, handle[0x%llx] type[%d].", __func__, handle,
            static_cast<int32_t>(res.type));
        return CCU_E_INTERNAL;
    }
    return_to_pool(*pool, res.res_ranges);
    return unmap_ret;
}

CcuResult ccu_var_event_res_mgr::release_by_instance(CcuInsHandle ins_handle)
{
    std::vector<ccu_var_event_res> to_release{};
    {
        std::unique_lock<std::shared_timed_mutex> lock(map_mutex_);
        for (auto it = res_map_.begin(); it != res_map_.end();) {
            if (it->second.ins_handle == ins_handle) {
                to_release.push_back(std::move(it->second));
                it = res_map_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 通信域正在销毁，单条失败不中断，记录首个错误码后继续清干净其余记录
    CcuResult first_err = CCU_SUCCESS;
    for (auto& res : to_release) {
        // ccu_instance 析构释放资源前，先解除 Alloc 阶段映射的进程可访问 VA
        CcuResult unmapRet = unmap_saved_addrs(res);
        if (unmapRet != CCU_SUCCESS && first_err == CCU_SUCCESS) {
            first_err = unmapRet;
        }

        if (res.res_repo == nullptr) {
            continue;
        }
        std::vector<HcommCcuResRangePod>* pool = select_pool(*res.res_repo, res.type, res.die_id);
        if (pool == nullptr) {
            HCCL_ERROR(
                "[CcuVarEventResMgr][%s] failed to return, ins_handle[0x%llx] type[%d].", __func__, ins_handle,
                static_cast<int32_t>(res.type));
            if (first_err == CCU_SUCCESS) {
                first_err = CCU_E_INTERNAL;
            }
            continue;
        }
        return_to_pool(*pool, res.res_ranges);
    }
    return first_err;
}

// 从空闲块列表 pool 中扣除区间 [start, start+count)必要时把命中的空闲块拆分成左右两段。
static void remove_range_from_pool(std::vector<HcommCcuResRangePod>& pool, uint32_t start, uint32_t count)
{
    if (count == 0) {
        return;
    }
    const uint32_t end = start + count;
    std::vector<HcommCcuResRangePod> result{};
    result.reserve(pool.size() + 1);
    for (const auto& block : pool) {
        const uint32_t blockStart = block.startId;
        const uint32_t blockEnd = block.startId + block.count;
        if (end <= blockStart || start >= blockEnd) {
            result.push_back(block);
            continue;
        }
        if (blockStart < start) {
            result.emplace_back(HcommCcuResRangePod{block.resourceType, block.dieId, blockStart, start - blockStart});
        }
        if (end < blockEnd) {
            result.emplace_back(HcommCcuResRangePod{block.resourceType, block.dieId, end, blockEnd - end});
        }
    }
    pool.swap(result);
}

CcuResult ccu_var_event_res_mgr::exclude_allocated_from_repo(CcuInsHandle ins_handle) const
{
    // shared_lock 用于只读遍历 res_map_；被修改的 *pool 属于该 ins_handle 自己的 AscCcuResSnapshot，
    // 其并发安全由“同一 instance 单线程串行访问”契约保证，详见头文件线程安全契约说明
    std::shared_lock<std::shared_timed_mutex> lock(map_mutex_);
    for (const auto& kv : res_map_) {
        const ccu_var_event_res& res = kv.second;
        if (res.ins_handle != ins_handle || res.res_repo == nullptr) {
            continue;
        }
        std::vector<HcommCcuResRangePod>* pool = select_pool(*res.res_repo, res.type, res.die_id);
        if (pool == nullptr) {
            HCCL_ERROR(
                "[CcuVarEventResMgr][%s] failed, ins_handle[0x%llx] type[%d].", __func__, ins_handle,
                static_cast<int32_t>(res.type));
            continue;
        }
        for (const auto& range : res.res_ranges) {
            remove_range_from_pool(*pool, range.startId, range.count);
            HCCL_INFO(
                "[CcuVarEventResMgr][%s] exclude acquired res, ins_handle[0x%llx] type[%d] "
                "die_id[%u] start_id[%u] count[%u].",
                __func__, ins_handle, static_cast<int32_t>(res.type), res.die_id, range.startId, range.count);
        }
    }
    return CCU_SUCCESS;
}

} // namespace asc
