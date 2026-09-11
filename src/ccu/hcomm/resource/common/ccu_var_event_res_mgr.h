/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_VAR_EVENT_RES_MGR_H
#define ASCCOMM_CCU_VAR_EVENT_RES_MGR_H

#include <shared_mutex>
#include <vector>
#include <unordered_map>

#include "ccu/hcomm/ccu_api_types.h"

#include "hcomm/resource/common/asc_ccu_resource_local.h"

namespace asc {

enum class ccu_var_event_type {
    variable = 0,
    event = 1,
};

struct ccu_var_event_res {
    CcuInsHandle ins_handle{0};
    int32_t dev_logic_id{-1};
    uint8_t die_id{0};
    ccu_var_event_type type{ccu_var_event_type::variable};
    std::vector<HcommCcuResRangePod> res_ranges{};
    // 借用指针，指向发起预约的 CcuKernelRegistry 名下 AscCcuResSnapshot 持有的资源仓，本类不持有所有权。
    // 有效性依赖既定时序：~CcuKernelRegistry 中 ReleaseByInstance 先于 resSnapshot_ 置空执行，
    // CcuKernelRegistry::Reset 调 ExcludeAllocatedFromRepo 时资源仓亦仍存活。
    // 新增读写该指针的路径时，须确认调用点仍在 AscCcuResSnapshot 析构之前。
    asc_ccu_res_repository* res_repo{nullptr};
    // 申请时对每个资源注册好的进程可访问 VA，下标与资源 index 一一对应
    std::vector<uint64_t> va_list{};
};

// 职责：管理通信域内 Variable(XN) / Event(CKE) 预约资源的完整生命周期，涵盖四个阶段——
// 从实例资源仓切出连续块、申请期为每个资源注册进程可访问 VA、按 handle 记账、随实例回收。
// 四者是同一份资源的先后阶段而非彼此独立的职责，故收敛在同一个类内：Acquire 完成
// 「切块 + 映射 + 记账」，ReleaseByHandle / ReleaseByInstance 完成对称的「解映射 + 归还」。
//
// 为何 Variable 与 Event 合用一个管理器：两类资源共享完全相同的使用模式——一次性预约连续块、
// 申请期暴露地址、句柄随通信域销毁统一回收；差异仅在资源池（blockXn / blockCke）与 runtime
// 资源类型（RT_RES_TYPE_CCU_XN / RT_RES_TYPE_CCU_CKE）两处，均由 CcuVarEventType 参数化。
// 拆成两个类会使上述四阶段流程与回滚逻辑重复一遍，故不拆。
//
// 与 CcuResIdAllocator 的边界：后者位于设备层，管理自持的 [0, capacity) 完整 id 空间，
// 其 resInfos_ 记录的是「已分配块」；本类位于实例层，从上游下发给本通信域的若干段「空闲块」
// 中做二次切分，无 capacity 概念。两者数据结构语义相反，不存在替代关系。
//
// 线程安全契约：
// 1) resMap_ 与 handleSeed_ 为 per-device、跨 CcuKernelRegistry 共享，全部读写均由 mapMutex_ 保护；
// 2) CcuVarEventRes::resRepo 指向的资源池由发起预约的 CcuKernelRegistry（经其 AscCcuResSnapshot）持有，
//    不在 mapMutex_ 的保护范围内。CcuKernelRegistry 自身不含锁，约定同一 instance 只由单线程串行
//    访问，池的并发安全由该约定保证；不同 instance 的池互不相交，跨 instance 并发是安全的。
// 因此 ExcludeAllocatedFromRepo 取 shared_lock 是为遍历 resMap_，而非保护池。
class ccu_var_event_res_mgr {
public:
    static ccu_var_event_res_mgr& get_instance(const int32_t device_logic_id);

    // 预约一段连续资源并在申请期完成地址映射；任一阶段失败均整笔回滚，仅全程成功才写 handle
    CcuResult acquire(
        CcuInsHandle ins_handle, asc_ccu_res_repository& res_repo, ccu_var_event_type type, uint8_t die_id,
        uint32_t num, uint64_t& handle);
    CcuResult get_variable_xn_id(uint64_t handle, uint32_t index, uint8_t& die_id, uint32_t& xn_id) const;
    CcuResult get_event_cke_id(uint64_t handle, uint32_t index, uint8_t& die_id, uint32_t& cke_id) const;
    // 保存 Alloc 阶段为该 handle 注册好的全部资源 VA（下标与资源 index 一一对应）
    CcuResult save_addrs(ccu_var_event_type type, uint64_t handle, const std::vector<uint64_t>& va_list);
    // 返回 Alloc 阶段已注册的第 index 个资源 VA
    CcuResult get_saved_addr(ccu_var_event_type type, uint64_t handle, uint32_t index, uint64_t& va) const;
    CcuResult release_by_handle(uint64_t handle);
    CcuResult release_by_instance(CcuInsHandle ins_handle);
    CcuResult exclude_allocated_from_repo(CcuInsHandle ins_handle) const;

private:
    explicit ccu_var_event_res_mgr() = default;
    ~ccu_var_event_res_mgr() = default;

    ccu_var_event_res_mgr(const ccu_var_event_res_mgr& that) = delete;
    ccu_var_event_res_mgr& operator=(const ccu_var_event_res_mgr& that) = delete;

    // 校验参数、选池、切出连续块并登记到 resMap_，成功时经 newHandle 返回新句柄
    CcuResult alloc_and_record(
        CcuInsHandle ins_handle, asc_ccu_res_repository& res_repo, ccu_var_event_type type, uint8_t die_id,
        uint32_t num, uint64_t& new_handle);
    // 为 handle 名下 num 个资源逐个注册进程可访问 VA 并缓存；失败时解除本次已完成的映射，
    // 资源池的归还由 Acquire 与切池动作配对完成
    CcuResult register_addrs(ccu_var_event_type type, uint64_t handle, uint32_t num);

    static CcuResult alloc_from_pool(
        std::vector<HcommCcuResRangePod>& pool, uint32_t num, std::vector<HcommCcuResRangePod>& out);
    static void return_to_pool(std::vector<HcommCcuResRangePod>& pool, const std::vector<HcommCcuResRangePod>& ranges);
    // 释放资源前，将 Alloc 阶段映射的进程可访问 VA 逐个 unmap（仅处理已保存 VA 的资源）；
    // 返回首个 unmap 失败的错误码，失败不中断，其余资源继续 unmap
    static CcuResult unmap_saved_addrs(const ccu_var_event_res& res);

    int32_t dev_logic_id_{-1};
    uint64_t handle_seed_{0};
    mutable std::shared_timed_mutex map_mutex_;
    std::unordered_map<uint64_t, ccu_var_event_res> res_map_{};
};

} // namespace asc

#endif // ASCCOMM_CCU_VAR_EVENT_RES_MGR_H
