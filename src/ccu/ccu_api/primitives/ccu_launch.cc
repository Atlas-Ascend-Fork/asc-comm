/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_api/ccu_launch.h"
#include "ccu/experimental/ccu_resource_api.h"

#include <vector>

// 同步 launch 需要 acl 运行时接口（rt 相关 API），与 runtime 头配合使用
#include "acl/acl_rt.h"
#include "runtime/rt_external_kernel.h"

#include "ccu_api/common/ccu_log.h"
#include "ccu_api/common/ccu_device_context.h"
#include "ccu/ccu_api/ccu_types.h"

#include "ccu_api/resource/kernel/ccu_kernel_mgr.h"
#include "ccu_api/resource/kernel/ccu_kernel_registry_mgr.h"
#include "ccu_api/resource/common/ccu_var_event_res_mgr.h"

#include "ccu_api/dfx/ccu_profiling.h"

// 在不进入内核注册流程的情况下准备单实例资源快照。
// 变量/事件分配接口可能先于 <<<>>> 调用，因此需要执行此操作。
static ccu_result ccu_prepare_instance(
    HcommCcuInstanceEntity ins_handle, asc::ccu_kernel_registry*& ccu_ins, int32_t& dev_logic_id)
{
    if (ins_handle.ccuInsKey == 0) {
        HCCL_ERROR(
            "[%s] failed, invalid instance handle: key[%llx].", __func__,
            static_cast<unsigned long long>(ins_handle.ccuInsKey));
        return ccu_result::CCU_E_PARA;
    }

    dev_logic_id = asc::get_current_ccu_device_logic_id();
    if (dev_logic_id < 0) {
        HCCL_ERROR("[%s] failed, current thread has no device set.", __func__);
        return ccu_result::CCU_E_UNAVAIL;
    }

    CCU_CHK_RET_ASC(
        asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get_or_create(dev_logic_id, ins_handle, ccu_ins));
    CCU_CHK_PTR_NULL_ASC(ccu_ins);
    return static_cast<ccu_result>(ccu_ins->load_register_context(ins_handle));
}

ccu_result asccomm_ccu_kernel_register_start(void* ins_handle)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    (void)asc::ccu_profiling::init_ccu_profiling_once();
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    // Instance 不再携带设备号：以当前线程设备定位 per-device registry/kernel mgr
    int32_t dev_logic_id = -1;
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET_ASC(ccu_prepare_instance(instanceHandle, ccu_ins, dev_logic_id));
    CCU_CHK_RET_ASC(ccu_ins->begin_register());

    ccu_result reset_ret = ccu_ins->reset();
    if (reset_ret != ccu_result::CCU_SUCCESS) {
        (void)ccu_ins->end_register();
        HCCL_ERROR("[%s] failed to reset instance res pool, ret[%d].", __func__, static_cast<ccu_result>(reset_ret));
        return static_cast<ccu_result>(reset_ret);
    }

    auto* res_snapshot = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL_ASC(res_snapshot);
    ccu_result ret = asc::ccu_kernel_mgr::get_instance(dev_logic_id).configure(*res_snapshot);
    if (ret != ccu_result::CCU_SUCCESS) {
        (void)ccu_ins->end_register();
        HCCL_ERROR("[%s] failed to load register context, ret[%d].", __func__, static_cast<ccu_result>(ret));
        return static_cast<ccu_result>(ret);
    }
    asc::set_current_ccu_control_ops(instanceHandle.ascCustom);
    return ccu_result::CCU_SUCCESS;
}

static ccu_result ccu_kernel_try_register(
    asc::ccu_kernel_registry* ccu_ins, asc::asc_ccu_res_snapshot* res_snapshot, uint32_t dev_logic_id, uint32_t die_id,
    const char* kernel_func_name, const void* kernel_func, const void** kernel_args, uint32_t arg_num,
    ccu_kernel_handle& new_handle)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    CCU_CHK_RET_ASC(kernel_mgr.register_kernel(
        *res_snapshot, die_id, kernel_func_name, kernel_func, kernel_args, arg_num, new_handle));
    CCU_CHK_RET_ASC(ccu_ins->save_kernel(new_handle));
    CCU_EXCEPTION_HANDLE_END_ASC
    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_kernel_register(
    void* ins_handle, uint32_t die_id, const char* kernel_func_name, const void* kernel_func, const void** kernel_args,
    uint32_t arg_num, ccu_kernel_handle* kernel_handle)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    HCCL_RUN_INFO("Entry-%s", __func__);

    CCU_CHK_PTR_NULL_ASC(kernel_func);
    CCU_CHK_PTR_NULL_ASC(kernel_handle);

    if (arg_num != 0) {
        CCU_CHK_PTR_NULL_ASC(kernel_args);
    }

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto* ccu_ins = asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get(instanceHandle);
    CCU_CHK_PTR_NULL_ASC(ccu_ins);

    CCU_CHK_RET_ASC(ccu_ins->check_registering());

    auto* res_snapshot = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL_ASC(res_snapshot);

    ccu_kernel_handle new_handle{0};
    const ccu_result ret = ccu_kernel_try_register(
        ccu_ins, res_snapshot, dev_logic_id, die_id, kernel_func_name, kernel_func, kernel_args, arg_num, new_handle);
    if (ret != ccu_result::CCU_SUCCESS) {
        ccu_ins->abort_register();
        if (ret == ccu_result::CCU_E_UNAVAIL) {
            HCCL_WARNING(
                "[%s] register kernel resource unavailable[%d], current register round aborted.", __func__, ret);
            return ccu_result::CCU_E_UNAVAIL;
        }
        HCCL_ERROR("[%s] failed, register kernel failed[%d], current register round aborted.", __func__, ret);
        return ret;
    }

    *kernel_handle = new_handle;
    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_kernel_register_end(void* ins_handle)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto* ccu_ins = asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get(instanceHandle);
    CCU_CHK_PTR_NULL_ASC(ccu_ins);

    CCU_CHK_RET_ASC(ccu_ins->end_register());
    const auto& new_kernels = ccu_ins->get_untranslated_kernels();

    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    // 当前翻译内部流程可能抛异常
    CCU_EXCEPTION_HANDLE_BEGIN
    CCU_CHK_RET_ASC(kernel_mgr.translate(new_kernels));
    CCU_EXCEPTION_HANDLE_END_ASC

    return ccu_result::CCU_SUCCESS;
}

// resNum 下标，对齐 hcomm HcommCcuResType 值 0~6（见 hcomm include/ccu/ccu_res_defs.h）
constexpr uint32_t CCU_RES_TYPE_LOOP = 0;        // LoopEngine 需求（含 block）
constexpr uint32_t CCU_RES_TYPE_CCU_BUF = 1;     // Memory Slice 需求（含 block）
constexpr uint32_t CCU_RES_TYPE_VARIABLE = 2;    // 变量需求（含 block）
constexpr uint32_t CCU_RES_TYPE_ADDRESS = 3;     // 通用存储地址需求（含 block）
constexpr uint32_t CCU_RES_TYPE_EVENT = 4;       // 完成事件/信号量需求（含 block 与 notify）
constexpr uint32_t CCU_RES_TYPE_CCU_THREAD = 5;  // mission 需求
constexpr uint32_t CCU_RES_TYPE_INSTRUCTION = 6; // 指令空间需求（含翻译器指令与常量）

ccu_result asccomm_ccu_kernel_query_res_req(
    void* ins_handle, uint32_t die_id, const void* kernel_func, const void** kernel_args, uint32_t arg_num,
    asccomm_ccu_res_desc* res_desc)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    HCCL_RUN_INFO("Entry-%s, die_id[%u]", __func__, die_id);

    CCU_CHK_PTR_NULL_ASC(kernel_func);
    CCU_CHK_PTR_NULL_ASC(res_desc);
    if (arg_num != 0) {
        CCU_CHK_PTR_NULL_ASC(kernel_args);
    }

    int32_t dev_logic_id = -1;
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET_ASC(ccu_prepare_instance(instanceHandle, ccu_ins, dev_logic_id));
    CCU_CHK_PTR_NULL_ASC(ccu_ins);

    asc::set_current_ccu_control_ops(instanceHandle.ascCustom);

    auto* res_snapshot = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL(res_snapshot);

    CCU_EXCEPTION_HANDLE_BEGIN
    asc::asc_ccu_res_request res_req{};
    uint32_t instr_count = 0;
    CCU_CHK_RET_ASC(
        asc::ccu_kernel_mgr::get_instance(dev_logic_id)
            .query_kernel_res_req(*res_snapshot, die_id, kernel_func, kernel_args, arg_num, res_req, instr_count));

    *res_desc = asccomm_ccu_res_desc{};
    res_desc->dieId = die_id;
    res_desc->resNum[CCU_RES_TYPE_LOOP] = res_req.count[asc::HCOMM_CCU_BATCH_RES_LOOP][die_id] +
                                          res_req.count[asc::HCOMM_CCU_BATCH_RES_BLOCK_LOOP][die_id];
    res_desc->resNum[CCU_RES_TYPE_CCU_BUF] =
        res_req.count[asc::HCOMM_CCU_BATCH_RES_MS][die_id] + res_req.count[asc::HCOMM_CCU_BATCH_RES_BLOCK_MS][die_id];
    res_desc->resNum[CCU_RES_TYPE_VARIABLE] =
        res_req.count[asc::HCOMM_CCU_BATCH_RES_XN][die_id] + res_req.count[asc::HCOMM_CCU_BATCH_RES_BLOCK_XN][die_id];
    res_desc->resNum[CCU_RES_TYPE_ADDRESS] =
        res_req.count[asc::HCOMM_CCU_BATCH_RES_GSA][die_id] + res_req.count[asc::HCOMM_CCU_BATCH_RES_BLOCK_GSA][die_id];
    res_desc->resNum[CCU_RES_TYPE_EVENT] =
        res_req.count[asc::HCOMM_CCU_BATCH_RES_CKE][die_id] + res_req.count[asc::HCOMM_CCU_BATCH_RES_BLOCK_CKE][die_id];
    res_desc->resNum[CCU_RES_TYPE_CCU_THREAD] = res_req.count[asc::HCOMM_CCU_BATCH_RES_MISSION][die_id];
    res_desc->resNum[CCU_RES_TYPE_INSTRUCTION] = instr_count;

    // 翻译器固定占用（对齐 register_start 时 build_translator_resource_request 的扣减）：
    // XN 144 / CKE 32 / GSA 48（V1），每 register_start 一次。与 resNum 分离返回，
    // mc2 聚合时仅加一次，避免多 kernel 重复计入。
    constexpr uint32_t slot_num = asc::ccu_rep::ccu_translator_mission_slot_num;
    constexpr uint32_t xn_per_slot = asc::ccu_rep::ccu_reference_manager_xn_num + asc::ccu_rep::ccu_translator_xn_num;
    constexpr uint32_t cke_per_slot = asc::ccu_rep::ccu_translator_cke_num;
    constexpr uint32_t gsa_per_slot = asc::ccu_rep::ccu_translator_gsa_num;
    res_desc->translatorResNum[CCU_RES_TYPE_VARIABLE] = slot_num * xn_per_slot;
    res_desc->translatorResNum[CCU_RES_TYPE_EVENT] = slot_num * cke_per_slot;
    if (res_snapshot->get_ccu_version() == asc::HCOMM_CCU_VERSION_V1) {
        res_desc->translatorResNum[CCU_RES_TYPE_ADDRESS] = slot_num * gsa_per_slot;
    }
    HCCL_INFO(
        "[%s] success, die_id[%u], loop[%u], ccu_buf[%u], variable[%u], address[%u], event[%u], ccu_thread[%u], "
        "instruction[%u].",
        __func__, die_id, res_desc->resNum[CCU_RES_TYPE_LOOP], res_desc->resNum[CCU_RES_TYPE_CCU_BUF],
        res_desc->resNum[CCU_RES_TYPE_VARIABLE], res_desc->resNum[CCU_RES_TYPE_ADDRESS],
        res_desc->resNum[CCU_RES_TYPE_EVENT], res_desc->resNum[CCU_RES_TYPE_CCU_THREAD],
        res_desc->resNum[CCU_RES_TYPE_INSTRUCTION]);
    CCU_EXCEPTION_HANDLE_END_ASC

    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_get_task_args_num(ccu_kernel_handle kernel_handle, uint32_t* task_args_num)
{
    HCCL_INFO("Entry-%s", __func__);
    CCU_CHK_PTR_NULL_ASC(task_args_num);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);

    asc::ccu_kernel_info info{};
    CCU_CHK_RET_ASC(kernel_mgr.get_ccu_kernel_info(kernel_handle, info));

    *task_args_num = info.max_task_args_num;
    HCCL_INFO("[%s] success, kernel_handle[%llx], task_args_num[%u].", __func__, kernel_handle, *task_args_num);
    return ccu_result::CCU_SUCCESS;
}

// notifywait 默认等待时长，27*68=1836，沿用 hcomm NOTIFY_DEFAULT_WAIT_TIME 的 notifywait 默认等待时长
constexpr uint32_t CCU_NOTIFY_DEFAULT_WAIT_TIME = 27U * 68U;

static ccu_result launch_ccu_tasks(const asc::ccu_task_param& param, const aclrtStream stream)
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
        HCCL_ERROR("[%s] failed to launch ccu, ret[%d]", __func__, static_cast<ccu_result>(ret));
        return ccu_result::CCU_E_INTERNAL;
    }

    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_kernel_launch(
    ccu_launch_stream stream, ccu_kernel_handle kernel_handle, const void* task_args, uint32_t arg_num)
{
    CCU_CHK_PTR_NULL_ASC(stream);
    CHK_PRT_RET(
        kernel_handle == 0, HCCL_ERROR("[%s] failed, kernel handle is empty.", __func__), ccu_result::CCU_E_PARA);
    CHK_PRT_RET(
        arg_num > 0 && task_args == nullptr,
        HCCL_ERROR("[%s] failed, taskArgs is nullptr while argNum[%u] > 0.", __func__, arg_num), ccu_result::CCU_E_PTR);

    // launch 使用调用线程实际所在设备，避免依赖数据面内部设备上下文与调用线程状态一致。
    const int32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    if (dev_logic_id < 0) {
        HCCL_ERROR("[%s] failed, current thread has no device set.", __func__);
        return ccu_result::CCU_E_UNAVAIL;
    }
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    auto* kernel = kernel_mgr.get_kernel(kernel_handle);
    CCU_CHK_PTR_NULL_ASC(kernel);

    auto stream_ptr = reinterpret_cast<aclrtStream>(stream);

    CCU_EXCEPTION_HANDLE_BEGIN
    std::vector<asc::ccu_task_param> task_params{};
    auto ret = kernel->gene_task_params(static_cast<const uint64_t*>(task_args), arg_num, task_params);
    CHK_PRT_RET(
        ret != ccu_result::CCU_SUCCESS, HCCL_ERROR("[%s] failed, kernel_handle[0x%llx].", __func__, kernel_handle),
        static_cast<ccu_result>(ret));

    if (task_params.empty()) {
        HCCL_INFO("[%s] passed, ccu params are empty.", __func__);
        return ccu_result::CCU_SUCCESS;
    }
    for (uint32_t idx = 0; idx < task_params.size(); idx++) {
        CCU_CHK_RET_ASC(launch_ccu_tasks(task_params[idx], stream_ptr));
    }
    CCU_EXCEPTION_HANDLE_END_ASC
    return ccu_result::CCU_SUCCESS;
}

// 定位实例资源仓后转发给 CcuVarEventResMgr；预约、地址映射与失败回滚均由该类内部完成
static ccu_result ccu_alloc_var_event_res(
    HcommCcuInstanceEntity ins_handle, asc::ccu_var_event_type type, uint8_t die_id, uint32_t num, uint64_t& out_handle)
{
    int32_t dev_logic_id = -1;
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET_ASC(ccu_prepare_instance(ins_handle, ccu_ins, dev_logic_id));

    auto* res_pack = ccu_ins->get_res_snapshot();
    CCU_CHK_PTR_NULL_ASC(res_pack);

    CCU_CHK_RET_ASC(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                        .acquire(ins_handle, res_pack->get_ccu_res_repo(), type, die_id, num, out_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_variable_reserved(
    void* ins_handle, uint8_t die_id, uint32_t num, ccu_variable_handle* var_handle)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    CCU_CHK_PTR_NULL_ASC(var_handle);
    *var_handle = 0;
    CCU_CHK_RET_ASC(
        ccu_alloc_var_event_res(instanceHandle, asc::ccu_var_event_type::variable, die_id, num, *var_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_event_reserved(void* ins_handle, uint8_t die_id, uint32_t num, ccu_event_handle* event_handle)
{
    CCU_CHK_PTR_NULL_ASC(ins_handle);
    HcommCcuInstanceEntity instanceHandle = *static_cast<HcommCcuInstanceEntity*>(ins_handle);
    CCU_CHK_PTR_NULL_ASC(event_handle);
    *event_handle = 0;
    CCU_CHK_RET_ASC(
        ccu_alloc_var_event_res(instanceHandle, asc::ccu_var_event_type::event, die_id, num, *event_handle));
    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_variable_get_addr(ccu_variable_handle var_handle, uint32_t index, uint64_t* va)
{
    CCU_CHK_PTR_NULL_ASC(va);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    CCU_CHK_RET_ASC(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                        .get_saved_addr(asc::ccu_var_event_type::variable, var_handle, index, *va));

    return ccu_result::CCU_SUCCESS;
}

ccu_result asccomm_ccu_event_get_addr(ccu_event_handle event_handle, uint32_t index, uint64_t* va)
{
    CCU_CHK_PTR_NULL_ASC(va);

    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    CCU_CHK_RET_ASC(asc::ccu_var_event_res_mgr::get_instance(dev_logic_id)
                        .get_saved_addr(asc::ccu_var_event_type::event, event_handle, index, *va));

    return ccu_result::CCU_SUCCESS;
}
