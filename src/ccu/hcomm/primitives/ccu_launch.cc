/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"

#include <vector>

// 同步 launch 需要 acl 运行时接口（rt 相关 API），与 runtime 头配合使用
#include "acl/acl_rt.h"
#include "runtime/rt_external_kernel.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_device_context.h"
#include "ccu/hcomm/ccu_api_types.h"

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/kernel/ccu_kernel_registry_mgr.h"
#include "hcomm/resource/common/ccu_var_event_res_mgr.h"

// 在不进入内核注册流程的情况下准备单实例资源快照。
// 变量/事件分配接口可能先于 <<<>>> 调用，因此需要执行此操作。
static CcuResult ccu_prepare_instance(
    CcuInsHandle ins_handle, asc::ccu_kernel_registry*& ccu_ins, int32_t& dev_logic_id)
{
    if (ins_handle.ccuInsKey == 0 || ins_handle.ccuInsPtr == nullptr) {
        HCCL_ERROR(
            "[%s] failed, invalid instance handle: key[%llx], pod[%p].", __func__,
            static_cast<unsigned long long>(ins_handle.ccuInsKey), static_cast<const void*>(ins_handle.ccuInsPtr));
        return CcuResult::CCU_E_PARA;
    }

    dev_logic_id = asc::get_current_ccu_device_logic_id();
    if (dev_logic_id < 0) {
        HCCL_ERROR("[%s] failed, current thread has no device set.", __func__);
        return CcuResult::CCU_E_UNAVAIL;
    }

    CCU_CHK_RET(
        asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get_or_create(dev_logic_id, ins_handle, ccu_ins));
    CCU_CHK_PTR_NULL(ccu_ins);
    return ccu_ins->load_register_context(*ins_handle.ccuInsPtr);
}

CcuResult asccomm_ccu_kernel_register_start(CcuInsHandle ins_handle)
{
    // Instance 不再携带设备号：以当前线程设备定位 per-device registry/kernel mgr
    int32_t dev_logic_id = -1;
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET(ccu_prepare_instance(ins_handle, ccu_ins, dev_logic_id));
    CCU_CHK_RET(ccu_ins->begin_register());

    auto* res_snapshot = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL(res_snapshot);
    CcuResult ret = asc::ccu_kernel_mgr::get_instance(dev_logic_id).configure(*res_snapshot);
    if (ret != CcuResult::CCU_SUCCESS) {
        (void)ccu_ins->end_register();
        HCCL_ERROR("[%s] failed to load register context, ret[%d].", __func__, ret);
        return ret;
    }
    asc::set_current_ccu_control_ops(ins_handle.ccuInsPtr->ascCustom);
    return CcuResult::CCU_SUCCESS;
}

static CcuResult ccu_kernel_try_register(
    asc::ccu_kernel_registry* ccu_ins, asc::asc_ccu_res_snapshot* res_snapshot, uint32_t dev_logic_id, uint32_t die_id,
    const char* kernel_func_name, const void* kernel_func, const void** kernel_args, uint32_t arg_num,
    ccu_kernel_handle& new_handle)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    CCU_CHK_RET(
        kernel_mgr.Register(*res_snapshot, die_id, kernel_func_name, kernel_func, kernel_args, arg_num, new_handle));
    CCU_CHK_RET(ccu_ins->save_kernel(new_handle));
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_kernel_register(
    CcuInsHandle ins_handle, uint32_t die_id, const char* kernel_func_name, const void* kernel_func,
    const void** kernel_args, uint32_t arg_num, ccu_kernel_handle* kernel_handle)
{
    HCCL_RUN_INFO("Entry-%s", __func__);

    CCU_CHK_PTR_NULL(kernel_func);
    CCU_CHK_PTR_NULL(kernel_handle);

    if (arg_num != 0) {
        CCU_CHK_PTR_NULL(kernel_args);
    }

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto* ccu_ins = asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get(ins_handle);
    CCU_CHK_PTR_NULL(ccu_ins);

    CCU_CHK_RET(ccu_ins->check_registering());

    auto* res_snapshot = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL(res_snapshot);

    ccu_kernel_handle new_handle{0};
    CcuResult ret = ccu_kernel_try_register(
        ccu_ins, res_snapshot, dev_logic_id, die_id, kernel_func_name, kernel_func, kernel_args, arg_num, new_handle);
    if (ret != CcuResult::CCU_SUCCESS) {
        ccu_ins->abort_register();
        if (CCU_CHK_RES_UNAVAIL(ret)) {
            HCCL_WARNING(
                "[%s] register kernel resource unavailable[%d], current register round aborted.", __func__, ret);
            return CcuResult::CCU_E_UNAVAIL;
        } else {
            HCCL_ERROR("[%s] failed, register kernel failed[%d], current register round aborted.", __func__, ret);
            return ret;
        }
    }

    *kernel_handle = new_handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_kernel_register_end(CcuInsHandle ins_handle)
{
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto* ccu_ins = asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get(ins_handle);
    CCU_CHK_PTR_NULL(ccu_ins);

    CCU_CHK_RET(ccu_ins->end_register());
    const auto& new_kernels = ccu_ins->get_untranslated_kernels();

    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    // 当前翻译内部流程可能抛异常
    CCU_EXCEPTION_HANDLE_BEGIN
    CCU_CHK_RET(kernel_mgr.translate(new_kernels));
    CCU_EXCEPTION_HANDLE_END

    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_get_task_args_num(ccu_kernel_handle kernel_handle, uint32_t* task_args_num)
{
    HCCL_INFO("Entry-%s", __func__);
    CCU_CHK_PTR_NULL(task_args_num);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);

    asc::ccu_kernel_info info{};
    CCU_CHK_RET(kernel_mgr.get_ccu_kernel_info(kernel_handle, info));

    *task_args_num = info.max_task_args_num;
    HCCL_INFO("[%s] success, kernelHandle[%llx], taskArgsNum[%u].", __func__, kernel_handle, *task_args_num);
    return CcuResult::CCU_SUCCESS;
}

// notifywait 默认等待时长，27*68=1836，沿用 hcomm NOTIFY_DEFAULT_WAIT_TIME 的 notifywait 默认等待时长
constexpr uint32_t CCU_NOTIFY_DEFAULT_WAIT_TIME = 27U * 68U;

static CcuResult launch_ccu_tasks(const asc::ccu_task_param& param, const aclrtStream stream)
{
    rtCcuTaskInfo_t task_info{};
    task_info.dieId = param.die_id;
    task_info.missionId = param.mission_id;
    task_info.instStartId = param.inst_start_id;
    task_info.instCnt = param.inst_cnt;
    task_info.key = param.key;
    task_info.argSize = param.arg_size;
    task_info.timeout = CCU_NOTIFY_DEFAULT_WAIT_TIME;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(task_info.args));

    auto ret = rtCCULaunch(&task_info, stream);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR("[%s] failed to launch ccu, ret[%d]", __func__, ret);
        return CcuResult::CCU_E_RUNTIME;
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_kernel_launch(
    ccu_launch_stream stream, ccu_kernel_handle kernel_handle, const void* task_args, uint32_t arg_num)
{
    CCU_CHK_PTR_NULL(stream);
    CHK_PRT_RET(
        kernel_handle == 0, HCCL_ERROR("[%s] failed, kernel handle is empty.", __func__), CcuResult::CCU_E_PARA);
    CHK_PRT_RET(
        arg_num > 0 && task_args == nullptr,
        HCCL_ERROR("[%s] failed, taskArgs is nullptr while argNum[%u] > 0.", __func__, arg_num), CcuResult::CCU_E_PTR);

    // launch 使用调用线程实际所在设备，避免依赖数据面内部设备上下文与调用线程状态一致。
    const int32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    if (dev_logic_id < 0) {
        HCCL_ERROR("[%s] failed, current thread has no device set.", __func__);
        return CcuResult::CCU_E_UNAVAIL;
    }
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    auto* kernel = kernel_mgr.get_kernel(kernel_handle);
    CCU_CHK_PTR_NULL(kernel);

    auto stream_ptr = reinterpret_cast<aclrtStream>(stream);

    CCU_EXCEPTION_HANDLE_BEGIN
    std::vector<asc::ccu_task_param> task_params{};
    auto ret = kernel->gene_task_params(static_cast<const uint64_t*>(task_args), arg_num, task_params);
    CHK_PRT_RET(
        ret != CcuResult::CCU_SUCCESS, HCCL_ERROR("[%s] failed, kernelHandle[0x%llx].", __func__, kernel_handle), ret);

    if (task_params.empty()) {
        HCCL_INFO("[%s] passed, ccu params are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }
    for (uint32_t idx = 0; idx < task_params.size(); idx++) {
        CCU_CHK_RET(launch_ccu_tasks(task_params[idx], stream_ptr));
    }
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_get_mem_token(uint64_t src_va, uint64_t size, uint64_t* token_info)
{
    CCU_CHK_PTR_NULL(token_info);

    if (src_va == 0 || size == 0) {
        HCCL_ERROR(
            "[%s] failed, srcVa[0x%llx] size[%llu] should not be 0.", __func__, static_cast<unsigned long long>(src_va),
            static_cast<unsigned long long>(size));
        return CcuResult::CCU_E_PARA;
    }
    // 注意token信息属于安全信息，均不允许打印
    // 查询与位域拼装收敛在 GetTokenInfo 内，此处仅转发；GetTokenInfo 会抛异常，故需异常拦截
    CCU_EXCEPTION_HANDLE_BEGIN
    *token_info = asc::ccu_rep::get_token_info(src_va, size);
    CCU_EXCEPTION_HANDLE_END

    return CcuResult::CCU_SUCCESS;
}

// 定位实例资源仓后转发给 CcuVarEventResMgr；预约、地址映射与失败回滚均由该类内部完成
static CcuResult ccu_alloc_var_event_res(
    CcuInsHandle ins_handle, asc::ccu_var_event_type type, uint8_t die_id, uint32_t num, uint64_t& out_handle)
{
    int32_t dev_logic_id = -1;
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET(ccu_prepare_instance(ins_handle, ccu_ins, dev_logic_id));

    auto* res_pack = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL(res_pack);

    CCU_CHK_RET(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                    .acquire(ins_handle, res_pack->get_ccu_res_repo(), type, die_id, num, out_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_variable_alloc(
    CcuInsHandle ins_handle, uint8_t die_id, uint32_t num, ccu_variable_handle* var_handle)
{
    CCU_CHK_PTR_NULL(var_handle);
    *var_handle = 0;
    CCU_CHK_RET(ccu_alloc_var_event_res(ins_handle, asc::ccu_var_event_type::variable, die_id, num, *var_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_event_alloc(CcuInsHandle ins_handle, uint8_t die_id, uint32_t num, ccu_event_handle* event_handle)
{
    CCU_CHK_PTR_NULL(event_handle);
    *event_handle = 0;
    CCU_CHK_RET(ccu_alloc_var_event_res(ins_handle, asc::ccu_var_event_type::event, die_id, num, *event_handle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_variable_get_addr(ccu_variable_handle var_handle, uint32_t index, uint64_t* va)
{
    CCU_CHK_PTR_NULL(va);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    CCU_CHK_RET(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                    .get_saved_addr(asc::ccu_var_event_type::variable, var_handle, index, *va));

    return CcuResult::CCU_SUCCESS;
}

CcuResult asccomm_ccu_event_get_addr(ccu_event_handle event_handle, uint32_t index, uint64_t* va)
{
    CCU_CHK_PTR_NULL(va);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    CCU_CHK_RET(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                    .get_saved_addr(asc::ccu_var_event_type::event, event_handle, index, *va));

    return CcuResult::CCU_SUCCESS;
}
