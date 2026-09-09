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

CcuResult ccu_kernel_registry::load_register_context(const HcommCcuRegisterContextPod& context)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    if (!res_snapshot_) {
        res_snapshot_.reset(new (std::nothrow) asc_ccu_res_snapshot());
        CCU_CHK_PTR_NULL(res_snapshot_);
    }
    // 上下文的设备号必须与创建时传入的一致，否则说明调用方把上下文用在了错误的实例上。
    if (context.deviceLogicId != dev_logic_id_) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, context deviceLogicId[%d] mismatches instance deviceLogicId[%d].",
            __func__, context.deviceLogicId, dev_logic_id_);
        return CcuResult::CCU_E_PARA;
    }
    CCU_CHK_RET(res_snapshot_->load(context));
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
    kernel_handles_.push_back(kernel_handle);
    untranslated_kernel_handles_.push_back(kernel_handle);
    return CcuResult::CCU_SUCCESS;
}

const std::vector<ccu_kernel_handle>& ccu_kernel_registry::get_untranslated_kernels()
{
    return untranslated_kernel_handles_;
}

CcuResult ccu_kernel_registry::begin_register()
{
    if (register_state_ == register_state::registering) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, previous register round is not ended, "
            "asccomm_ccu_kernel_register_end is missing before a new asccomm_ccu_kernel_register_start.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    register_state_ = register_state::registering;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry::check_registering() const
{
    if (register_state_ != register_state::registering) {
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
    if (register_state_ == register_state::idle) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, asccomm_ccu_kernel_register_end is called without a matching "
            "asccomm_ccu_kernel_register_start.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (register_state_ == register_state::register_aborted) {
        HCCL_WARNING(
            "[CcuKernelRegistry][%s] previous register round was aborted due to error, "
            "close it to keep Start/End paired, no kernel will be translated.",
            __func__);
    }
    register_state_ = register_state::idle;
    return CcuResult::CCU_SUCCESS;
}

void ccu_kernel_registry::abort_register()
{
    for (auto kernel_handle : untranslated_kernel_handles_) {
        if (kernel_handle == 0) {
            continue;
        }
        (void)ccu_kernel_mgr::get_instance(dev_logic_id_).un_register(kernel_handle);
        auto it = std::find(kernel_handles_.begin(), kernel_handles_.end(), kernel_handle);
        if (it != kernel_handles_.end()) {
            kernel_handles_.erase(it);
        }
    }
    untranslated_kernel_handles_.clear();
    register_state_ = register_state::register_aborted;
}

} // namespace asc
