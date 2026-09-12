/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_registry.h"

#include <algorithm>

#include "hcomm/common/ccu_log.h"

#include "hcomm/resource/common/asc_ccu_res_snapshot.h"
#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/common/ccu_var_event_res_mgr.h"

namespace asc {

ccu_kernel_registry::~ccu_kernel_registry()
{
    // 主动释放资源保证时序，不得随意调整顺序
    for (auto& kernel_handle : kernel_handles_) {
        if (kernel_handle != 0) {
            (void)ccu_kernel_mgr::get_instance(dev_logic_id_).un_register(kernel_handle);
            kernel_handle = 0;
        }
    }
    kernel_handles_.clear();

    // 必须先于资源快照释放：CcuVarEventResMgr 持有指向快照内资源仓的借用指针。
    (void)ccu_var_event_res_mgr::get_instance(dev_logic_id_).release_by_instance(ins_handle_);

    res_snapshot_ = nullptr;
}

CcuResult ccu_kernel_registry::init(int32_t device_logic_id)
{
    // 先于 LoadRegisterContext 落定设备号：实例可能在未加载上下文的情况下被销毁，
    // 此时析构仍需按正确设备定位 CcuKernelMgr。
    dev_logic_id_ = device_logic_id;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::load_register_context(
    const HcommCcuRegisterContextPod &context, bool &requires_configuration)
{
    requires_configuration = false;
    CCU_EXCEPTION_HANDLE_BEGIN
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id()) {
        return CcuResult::CCU_E_INTERNAL;
    }
    // 上下文的设备号必须与创建时传入的一致，否则说明调用方把上下文用在了错误的实例上。
    if (context.deviceLogicId != dev_logic_id_) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, context deviceLogicId[%d] mismatches instance deviceLogicId[%d].",
            __func__, context.deviceLogicId, dev_logic_id_);
        return CcuResult::CCU_E_PARA;
    }

    std::unique_ptr<asc_ccu_res_snapshot> candidate(new (std::nothrow) asc_ccu_res_snapshot());
    CCU_CHK_PTR_NULL(candidate);
    CCU_CHK_RET(candidate->load(context));
    if (res_snapshot_) {
        if (res_snapshot_->get_generation() != candidate->get_generation() ||
            res_snapshot_->get_ccu_version() != candidate->get_ccu_version() ||
            res_snapshot_->get_valid_die_mask() != candidate->get_valid_die_mask()) {
            HCCL_ERROR("[CcuKernelRegistry][%s] register context changed without releasing the instance.", __func__);
            return CcuResult::CCU_E_PARA;
        }
        requires_configuration = !configured_;
        return CcuResult::CCU_SUCCESS;
    }

    res_snapshot_ = std::move(candidate);
    requires_configuration = true;
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::reset()
{
    if (!res_snapshot_) {
        return CcuResult::CCU_E_UNAVAIL;
    }

    untranslated_kernel_handles_.clear();
    CCU_CHK_RET(res_snapshot_->reset());

    // 刷新余量池后，把已被 Variable/Event 预约占用的区间从池中排除。
    CCU_CHK_RET(ccu_var_event_res_mgr::get_instance(dev_logic_id_).exclude_allocated_from_repo(ins_handle_));
    return CcuResult::CCU_SUCCESS;
}

asc_ccu_res_snapshot* ccu_kernel_registry::get_res_snapshot() { return res_snapshot_.get(); }

void ccu_kernel_registry::set_handle(CcuInsHandle ins_handle) { ins_handle_ = ins_handle; }

CcuResult ccu_kernel_registry::save_kernel(const ccu_kernel_handle kernel_handle)
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id()) {
        return CcuResult::CCU_E_INTERNAL;
    }
    try {
        kernel_handles_.push_back(kernel_handle);
        try {
            untranslated_kernel_handles_.push_back(kernel_handle);
        } catch (...) {
            kernel_handles_.pop_back();
            throw;
        }
    } catch (...) {
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::get_untranslated_kernels(std::vector<ccu_kernel_handle> &kernel_handles) const
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id()) {
        return CcuResult::CCU_E_INTERNAL;
    }
    try {
        kernel_handles = untranslated_kernel_handles_;
    } catch (...) {
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::checkpoint_register_resources()
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id() ||
        !res_snapshot_) {
        return CcuResult::CCU_E_INTERNAL;
    }
    CCU_EXCEPTION_HANDLE_BEGIN
    register_resource_checkpoint_ = res_snapshot_->get_ccu_res_repo();
    register_resource_checkpoint_valid_ = true;
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::mark_configured()
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id() ||
        !res_snapshot_) {
        return CcuResult::CCU_E_INTERNAL;
    }
    configured_ = true;
    return CcuResult::CCU_SUCCESS;
}

uint64_t ccu_kernel_registry::get_generation() const
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    return res_snapshot_ == nullptr ? 0 : res_snapshot_->get_generation();
}

CcuResult ccu_kernel_registry::begin_register()
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::idle) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, previous register round is not ended, "
            "asccomm_ccu_kernel_register_end is missing before a new asccomm_ccu_kernel_register_start.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    register_state_ = register_state::registering;
    register_owner_ = std::this_thread::get_id();
    untranslated_kernel_handles_.clear();
    register_resource_checkpoint_ = {};
    register_resource_checkpoint_valid_ = false;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::check_registering() const
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id()) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, asccomm_ccu_kernel_register must be called between "
            "asccomm_ccu_kernel_register_start and asccomm_ccu_kernel_register_end.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::end_register()
{
    std::lock_guard<std::mutex> lock(register_mutex_);
    if (register_state_ == register_state::idle) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, asccomm_ccu_kernel_register_end is called without a matching "
            "asccomm_ccu_kernel_register_start.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (register_owner_ != std::this_thread::get_id()) {
        HCCL_ERROR("[CcuKernelRegistry][%s] failed, register round must be ended by its owner thread.", __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (register_state_ == register_state::register_aborted) {
        HCCL_WARNING(
            "[CcuKernelRegistry][%s] previous register round was aborted due to error, "
            "close it to keep Start/End paired, no kernel will be translated.",
            __func__);
    }
    untranslated_kernel_handles_.clear();
    register_resource_checkpoint_valid_ = false;
    register_state_ = register_state::idle;
    register_owner_ = std::thread::id{};
    return CcuResult::CCU_SUCCESS;
}

void ccu_kernel_registry::abort_register()
{
    std::vector<ccu_kernel_handle> aborted_handles;
    {
        std::lock_guard<std::mutex> lock(register_mutex_);
        if (register_state_ != register_state::registering || register_owner_ != std::this_thread::get_id()) {
            return;
        }
        aborted_handles.swap(untranslated_kernel_handles_);
        if (register_resource_checkpoint_valid_ && res_snapshot_) {
            res_snapshot_->get_ccu_res_repo() = std::move(register_resource_checkpoint_);
        }
        for (auto kernel_handle : aborted_handles) {
            auto it = std::find(kernel_handles_.begin(), kernel_handles_.end(), kernel_handle);
            if (it != kernel_handles_.end()) {
                kernel_handles_.erase(it);
            }
        }
        register_resource_checkpoint_valid_ = false;
        register_state_ = register_state::register_aborted;
    }

    for (auto kernel_handle : aborted_handles) {
        if (kernel_handle == 0) {
            continue;
        }
        (void)ccu_kernel_mgr::get_instance(dev_logic_id_).un_register(kernel_handle);
    }
}

} // namespace asc
