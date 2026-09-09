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
#include "hcomm/hcomm_ccu_launch.h"

#include <chrono>
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

#include "hcomm/resource/microcode/ccu_assist_v1.h"

CcuResult asccomm_ccu_kernel_register_start(CcuInsHandle ins_handle, HcommCcuRegisterContextHandle context_handle)
{
    CCU_CHK_PTR_NULL(context_handle);
    const auto* context = reinterpret_cast<const HcommCcuRegisterContextPod*>(context_handle);
    if (context->instanceHandle != 0 && context->instanceHandle != ins_handle) {
        HCCL_ERROR(
            "[%s] failed, context instanceHandle[%llu] mismatches input insHandle[%llu].", __func__,
            static_cast<unsigned long long>(context->instanceHandle), static_cast<unsigned long long>(ins_handle));
        return CcuResult::CCU_E_PARA;
    }
    asc::ccu_kernel_registry* ccu_ins = nullptr;
    CCU_CHK_RET(asc::ccu_kernel_registry_mgr::get_instance(context->deviceLogicId)
                    .get_or_create(context->deviceLogicId, ins_handle, ccu_ins));
    CCU_CHK_PTR_NULL(ccu_ins);
    CCU_CHK_RET(ccu_ins->begin_register());

    CcuResult ret = ccu_ins->load_register_context(*context);
    if (ret == CcuResult::CCU_SUCCESS) {
        auto* res_snapshot = ccu_ins->get_res_snapshot();
        CCU_CHK_PTR_NULL(res_snapshot);
        ret = asc::ccu_kernel_mgr::get_instance(context->deviceLogicId).configure(*res_snapshot);
    }
    if (ret != CcuResult::CCU_SUCCESS) {
        (void)ccu_ins->end_register();
        HCCL_ERROR("[%s] failed to load register context, ret[%d].", __func__, ret);
        return ret;
    }
    asc::set_current_ccu_device_logic_id(context->deviceLogicId);
    asc::set_current_ccu_control_ops(ccu_ins->get_res_snapshot()->get_control_ops());
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
    const auto start_time = std::chrono::steady_clock::now();

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
    const auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, duration.count());
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

static CcuResult construct_ccu_detail_info(
    const std::vector<asc::ccu_profiling_info>& all_ccu_profiling_info, bool is_save_profiling_data,
    std::vector<HcommCcuProfileDetailPod>& converted)
{
    if (all_ccu_profiling_info.empty() || !is_save_profiling_data) {
        return CcuResult::CCU_SUCCESS;
    }

    converted.resize(all_ccu_profiling_info.size());
    // profiling 明细跨动态库时只传递 POD，hcomm 在 callback 前再还原为原有 C++ 类型。
    for (uint32_t idx = 0; idx < all_ccu_profiling_info.size(); ++idx) {
        const auto& src = all_ccu_profiling_info[idx];
        auto& dst = converted[idx];
        if (src.name.size() >= HCOMM_CCU_PROFILE_NAME_CAPACITY) {
            HCCL_ERROR(
                "[%s] profiling name length[%zu] exceeds maximum[%u].", __func__, src.name.size(),
                HCOMM_CCU_PROFILE_NAME_CAPACITY - 1U);
            return CcuResult::CCU_E_PARA;
        }
        dst.header = {HCOMM_CCU_LAUNCH_ABI_VERSION, HCOMM_CCU_PROFILE_DETAIL_MAGIC_WORD, sizeof(dst), 0};
        dst.nameLength = static_cast<uint32_t>(src.name.size());
        (void)memcpy_s(dst.name, sizeof(dst.name), src.name.data(), src.name.size());
        dst.profilingType = src.type;
        dst.dieId = src.die_id;
        dst.missionId = src.mission_id;
        dst.instructionId = src.instr_id;
        dst.reduceOpType = src.reduce_op_type;
        dst.inputDataType = src.input_data_type;
        dst.outputDataType = src.output_data_type;
        dst.dataSize = src.data_size;
        dst.ckeId = src.cke_id;
        dst.mask = src.mask;
        (void)memcpy_s(dst.channelId, sizeof(dst.channelId), src.channel_id, sizeof(src.channel_id));
        (void)memcpy_s(dst.remoteRankId, sizeof(dst.remoteRankId), src.remote_rank_id, sizeof(src.remote_rank_id));
        (void)memcpy_s(dst.channelHandle, sizeof(dst.channelHandle), src.channel_handle, sizeof(src.channel_handle));
    }
    return CcuResult::CCU_SUCCESS;
}

static HcommCcuTaskProfilePod construct_ccu_task_profile(
    const asc::ccu_task_param& ccu_param, const ccu_kernel_handle kernel_handle, uint64_t begin_time, uint64_t end_time,
    bool is_master)
{
    // POD 头部固定携带版本、类型和大小，供 hcomm 在解释后续 profiling 字段前校验 ABI。
    HcommCcuTaskProfilePod task{};
    task.header = {HCOMM_CCU_LAUNCH_ABI_VERSION, HCOMM_CCU_TASK_PROFILE_MAGIC_WORD, sizeof(task), 0};
    task.beginCycle = begin_time;
    task.endCycle = end_time;
    task.dieId = ccu_param.die_id;
    task.missionId = ccu_param.mission_id;
    task.instructionId = ccu_param.inst_start_id;
    task.isMaster = is_master ? 1U : 0U;
    task.kernelHandle = kernel_handle;
    task.diagnose = asc::asccomm_ccu_diagnose;
    task.reserved = 0;
    return task;
}

static void log_ccu_task_info(
    const std::vector<asc::ccu_task_param>& ccu_params, const ccu_kernel_handle kernel_handle,
    uint32_t exec_time_out_sec)
{
    if (CheckLogLevel(asccomm_ccu_log_module_id, DLOG_INFO) != 1) {
        return;
    }
    for (uint32_t idx = 0; idx < ccu_params.size(); idx++) {
        const auto& param = ccu_params[idx];
        HCCL_INFO(
            "[%s] start ccu task, dieId[%u], missionId[%u], execMissionId[%u], instStartId[%u], instCnt[%u], "
            "argSize[%u], timeout[%u]s, executeId[0x%llx], ccuKernelHandle[0x%llx]",
            __func__, param.die_id, param.mission_id, param.mission_id, param.inst_start_id, param.inst_cnt,
            param.arg_size, exec_time_out_sec, kernel_handle, kernel_handle);
    }
}

static void construct_profiling_info_log(const std::vector<asc::ccu_profiling_info>& all_ccu_profiling_info)
{
    if (CheckLogLevel(asccomm_ccu_log_module_id, DLOG_INFO) != 1) {
        return;
    }
    for (const asc::ccu_profiling_info& prof_info : all_ccu_profiling_info) {
        for (int idx = 0; idx < asc::ccu_max_channel_num; idx++) {
            if (prof_info.channel_id[idx] == asc::invalid_value_channelid) {
                break;
            }
            HCCL_INFO(
                "[%s]idx[%d]: channelId[%u], channelHandle[0x%llx]", __func__, idx, prof_info.channel_id[idx],
                prof_info.channel_handle[idx]);
        }
    }
}

static CcuResult construct_profiling_info(
    asc::ccu_kernel* kernel, const uint64_t* task_args, uint32_t arg_num,
    std::vector<asc::ccu_profiling_info>& all_ccu_profiling_info, bool is_save_profiling_data)
{
    if (!is_save_profiling_data) {
        return CcuResult::CCU_SUCCESS;
    }

    CCU_CHK_RET(kernel->get_ccu_profiling_info(task_args, arg_num, all_ccu_profiling_info));
    if (all_ccu_profiling_info.empty()) {
        return CcuResult::CCU_SUCCESS;
    }
    construct_profiling_info_log(all_ccu_profiling_info);
    return CcuResult::CCU_SUCCESS;
}

static CcuResult launch_ccu_tasks(
    const asc::ccu_task_param& param, const aclrtStream stream, uint32_t exec_time_out_sec)
{
    rtCcuTaskInfo_t task_info{};
    task_info.dieId = param.die_id;
    task_info.missionId = param.mission_id;
    task_info.instStartId = param.inst_start_id;
    task_info.instCnt = param.inst_cnt;
    task_info.key = param.key;
    task_info.argSize = param.arg_size;
    task_info.timeout = exec_time_out_sec;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(task_info.args));

    auto ret = rtCCULaunch(&task_info, stream);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR("[%s] failed to launch ccu, ret[%d]", __func__, ret);
        return CcuResult::CCU_E_RUNTIME;
    }

    return CcuResult::CCU_SUCCESS;
}

static bool is_launch_context_valid(const HcommCcuLaunchContextPod& context)
{
    const auto& header = context.header;
    return header.version == HCOMM_CCU_LAUNCH_ABI_VERSION && header.magicWord == HCOMM_CCU_LAUNCH_CONTEXT_MAGIC_WORD &&
           header.size == sizeof(context) && header.reserved == 0 && context.reserved[0] == 0 &&
           context.reserved[1] == 0 && context.runtimeStream != 0 && context.threadHandle != 0;
}

CcuResult asccomm_ccu_kernel_launch(
    const HcommCcuLaunchContextPod* launch_context, ccu_kernel_handle kernel_handle, const void* task_args,
    uint32_t arg_num)
{
    const auto start_time = std::chrono::steady_clock::now();

    CCU_CHK_PTR_NULL(launch_context);
    CHK_PRT_RET(
        kernel_handle == 0, HCCL_ERROR("[%s] failed, kernel handle is empty.", __func__), CcuResult::CCU_E_PARA);
    CHK_PRT_RET(
        arg_num > 0 && task_args == nullptr,
        HCCL_ERROR("[%s] failed, taskArgs is nullptr while argNum[%u] > 0.", __func__, arg_num), CcuResult::CCU_E_PTR);

    // 跨动态库边界先完整校验 POD，避免按不兼容布局读取 stream、设备号和 profiling 状态。
    CHK_PRT_RET(
        !is_launch_context_valid(*launch_context),
        HCCL_ERROR("[%s] failed, launch context has an incompatible ABI header or runtime stream.", __func__),
        CcuResult::CCU_E_PARA);
    HCCL_INFO(
        "[asccomm_ccu_kernel_launch] threadHandle[0x%llx] kernelHandle[0x%llx].", launch_context->threadHandle,
        kernel_handle);
    // runtimeStream 是 hcomm 借出的运行时 stream，且仅在本次同步 launch 返回前有效。
    auto stream_ptr = reinterpret_cast<aclrtStream>(launch_context->runtimeStream);

    // launch 使用线程实际所属设备，避免依赖 asc-comm 内部设备上下文与调用线程状态一致。
    const int32_t dev_logic_id = launch_context->deviceLogicId;
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    auto* kernel = kernel_mgr.get_kernel(kernel_handle);
    CCU_CHK_PTR_NULL(kernel);

    CCU_EXCEPTION_HANDLE_BEGIN
    std::vector<asc::ccu_task_param> task_params{};
    auto ret = kernel->gene_task_params(static_cast<const uint64_t*>(task_args), arg_num, task_params);
    CHK_PRT_RET(
        ret != CcuResult::CCU_SUCCESS,
        HCCL_ERROR(
            "[%s] failed, threadHandle[0x%llx] kernelHandle[0x%llx].", __func__, launch_context->threadHandle,
            kernel_handle),
        ret);

    if (task_params.empty()) {
        HCCL_INFO("[%s] passed, ccu params are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }
    const bool is_save_profiling_data = launch_context->profilingL1Enabled != 0 ||
                                        launch_context->profilingL0Enabled != 0 || launch_context->profilingCached != 0;

    std::vector<asc::ccu_profiling_info> all_ccu_profiling_info;
    CCU_CHK_RET(construct_profiling_info(
        kernel, static_cast<const uint64_t*>(task_args), arg_num, all_ccu_profiling_info, is_save_profiling_data));
    log_ccu_task_info(task_params, kernel_handle, launch_context->timeoutSec);
    std::vector<HcommCcuProfileDetailPod> ccu_detail_info;
    CCU_CHK_RET(construct_ccu_detail_info(all_ccu_profiling_info, is_save_profiling_data, ccu_detail_info));
    for (uint32_t idx = 0; idx < task_params.size(); idx++) {
        uint64_t begin_time = 0;
        uint64_t end_time = 0;
        if (launch_context->getProfilingCycle != nullptr) {
            CCU_CHK_RET(launch_context->getProfilingCycle(&begin_time));
        }
        CCU_CHK_RET(launch_ccu_tasks(task_params[idx], stream_ptr, launch_context->timeoutSec));
        if (launch_context->getProfilingCycle != nullptr) {
            CCU_CHK_RET(launch_context->getProfilingCycle(&end_time));
        }
        const HcommCcuTaskProfilePod task_profile = construct_ccu_task_profile(
            task_params[idx], kernel_handle, begin_time, end_time, launch_context->isMaster != 0);
        if (launch_context->reportTask != nullptr) {
            CCU_CHK_RET(launch_context->reportTask(
                launch_context->threadHandle, &task_profile, ccu_detail_info.empty() ? nullptr : ccu_detail_info.data(),
                static_cast<uint32_t>(ccu_detail_info.size())));
        }
    }
    CCU_EXCEPTION_HANDLE_END
    const auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
    HCCL_INFO("[%s] success, take time [%lld]us.", __func__, duration.count());
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
    const uint32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    auto* ccu_ins = asc::ccu_kernel_registry_mgr::get_instance(dev_logic_id).get(ins_handle);
    CCU_CHK_PTR_NULL(ccu_ins);

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
