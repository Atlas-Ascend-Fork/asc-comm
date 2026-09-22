/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel.h"

#include <algorithm>
#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"

#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"

#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funccall_v1.h"
#include "hcomm/resource/common/ccu_var_event_res_mgr.h"
#include "hcomm/resource/channel/ccu_channel.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_device_context.h"

// todo: 引入头文件需要检查
#include "hcomm/resource/microcode/ccu_assist_v1.h"
// 以下头文件引入的类型在本文件中未使用，控制面头已移回 hcomm，后续统一清理
// #include "hccl_comm_pub.h"
// #include "hcclCommDfx.h"
// #include "task_info.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

namespace asc {

constexpr uint32_t token_value_index = 2;
constexpr uint16_t invalid_u16 = 65535;
constexpr uint32_t max_loop_engine_pool_size_v1 = 128;
constexpr uint32_t max_loop_engine_pool_size_v2 = 512;

using ccu_rep::ccu_ins_generater_base;
using ccu_rep::ccu_ins_generater_v1;

template <typename t>
t ccu_kernel::create_res_assist(std::array<std::vector<t>, ccu_max_iodie_num>& res_record)
{
    // kernel确认die之前默认为0，需要刷新资源
    // 确认die之后按实际使用die分配资源
    const uint32_t die_id = get_die_id();
    res_record[die_id].emplace_back(this);
    auto& item = res_record[die_id].back();
    item.reset(res_record[die_id].size(), die_id);
    return item;
}

template <typename t>
std::vector<t> ccu_kernel::create_block_res_assist(
    const uint32_t count, std::array<std::vector<t>, ccu_max_iodie_num>& res_record)
{
    constexpr uint16_t ccu_block_res_id_base = 0x1000; // block 批量分配资源 id 基址，与单资源 id 区间隔离便于 DFX 定位
    std::vector<t> block;
    block.reserve(count);
    const uint32_t die_id = get_die_id();
    for (size_t i = 0; i < count; i++) {
        block.emplace_back(this);
        block.back().reset(static_cast<uint16_t>(ccu_block_res_id_base + res_record[die_id].size() + i), die_id);
    }
    res_record[die_id].insert(res_record[die_id].end(), block.begin(), block.end());
    return block;
}

ccu_kernel::~ccu_kernel() {}

static HcclResult get_die_id_by_channel(const ChannelHandle channel, uint32_t& die_id)
{
    ccu_channel channel_impl(channel);
    CHK_RET(channel_impl.get_result());
    die_id = channel_impl->get_die_id();
    HCCL_INFO("[%s], channelHandle[0x%llx], dieId[%u]", __func__, channel, die_id);
    return HcclResult::HCCL_SUCCESS;
}

static HcclResult get_die_id_by_channels(
    const std::unordered_set<ChannelHandle>& channels, uint32_t valid_die_mask, uint32_t& die_id)
{
    if (channels.empty()) {
        for (uint32_t die = 0; die < ccu_max_iodie_num; die++) {
            if ((valid_die_mask & (1U << die)) != 0) {
                die_id = die;
                return HcclResult::HCCL_SUCCESS;
            }
        }

        HCCL_ERROR("[CcuKernel][%s] failed, all dies are disabled, validDieMask[0x%x].", __func__, valid_die_mask);
        return HcclResult::HCCL_E_INTERNAL;
    }

    uint32_t first_die_id = 0;
    CHK_RET(get_die_id_by_channel(*channels.begin(), first_die_id));
    for (const auto channel : channels) {
        uint32_t next_die_id = 0;
        CHK_RET(get_die_id_by_channel(channel, next_die_id));
        if (first_die_id != next_die_id) {
            HCCL_ERROR("[%s] failed, the dies of channels are not same.", __func__);
            return HcclResult::HCCL_E_PARA;
        }
    }

    die_id = first_die_id;
    return HcclResult::HCCL_SUCCESS;
}

static HcclResult check_channels_die(const std::unordered_set<ChannelHandle>& channels, const uint32_t target_die_id)
{
    for (const auto channel : channels) {
        uint32_t channel_die_id = 0;
        CHK_RET(get_die_id_by_channel(channel, channel_die_id));
        if (channel_die_id != target_die_id) {
            HCCL_ERROR(
                "[%s] failed, channel[0x%llx] dieId[%u] differs from target dieId[%u].", __func__, channel,
                channel_die_id, target_die_id);
            return HcclResult::HCCL_E_PARA;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

static void move_resources_to_die(ccu_rep_resource& res, uint32_t target_die_id)
{
    if (target_die_id == 0)
        return; // 初始资源位于die0，不用设置

    auto move_and_set = [&target_die_id](auto& arr) {
        arr[target_die_id] = std::move(arr[0]);
        for (auto& item : arr[target_die_id])
            item.set_die_id(target_die_id);
    };

    move_and_set(res.ccubufs);
    move_and_set(res.block_ccubufs);
    move_and_set(res.executor);
    move_and_set(res.block_executor);
    move_and_set(res.completed_event);
    move_and_set(res.block_completed_event);
    move_and_set(res.address);
    move_and_set(res.block_address);
    move_and_set(res.continuous_variable);
    move_and_set(res.variable);
    move_and_set(res.local_notify);
}

HcclResult ccu_kernel::setup_profiling_info(const char* kernel_func_name)
{
    if (kernel_func_name == nullptr || strlen(kernel_func_name) == 0) {
        name_ = std::string("CCU_KERNEL"); // 默认名称
        add_sqe_profiling(name_);
        return HcclResult::HCCL_SUCCESS;
    }

    constexpr size_t max_kernel_func_name_len = 128;
    const auto name_len = strlen(kernel_func_name);
    if (name_len > max_kernel_func_name_len) {
        name_ = std::string(kernel_func_name, max_kernel_func_name_len);
        HCCL_WARNING("[CcuKernel][%s] kernelFuncName is too long, reset to %s.", __func__, name_.c_str());
    }

    // 生成SQE粒度profiling信息，此时未选择die，默认die 0
    add_sqe_profiling(name_);
    return HcclResult::HCCL_SUCCESS;
}

static HcclResult update_profiling_info(
    std::vector<ccu_profiling_info>& profiling_infos, uint32_t die_id, const std::string& kernel_name)
{
    if (die_id == 0) {
        // 与默认dieId相同，不需要修改
        return HcclResult::HCCL_SUCCESS;
    }

    // 正常情况仅首个info包含die信息，仅应为CCU_TASK_PROFILING类型
    if (UNLIKELY(profiling_infos.empty())) {
        // profiling不属于主流程，不打断算子业务
        HCCL_INFO("[%s] passed, profiling infos are empty, ccu kernel func[%s].", __func__, kernel_name.c_str());
        return HcclResult::HCCL_SUCCESS;
    }

    // 根据选择的die跟新profiling信息
    for (auto& info : profiling_infos) {
        info.die_id = die_id;
    }

    HCCL_INFO("[%s] reset profiling info dieId to [%u], ccu kernel func[%s].", __func__, die_id, kernel_name.c_str());
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel::apply_die_from_channels(uint32_t valid_die_mask)
{
    uint32_t die_id{0};
    CHK_RET(get_die_id_by_channels(channels_, valid_die_mask, die_id));
    CHK_PRT_RET(
        die_id >= ccu_max_iodie_num,
        HCCL_ERROR("[CcuKernel][%s] failed, dieId[%u] should be less than [%u].", __func__, die_id, ccu_max_iodie_num),
        HcclResult::HCCL_E_PARA);
    set_die_id(die_id);
    move_resources_to_die(res_, die_id);
    (void)update_profiling_info(profiling_info, die_id, name_);

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel::validate_and_apply_die(const uint32_t target_die_id, uint32_t valid_die_mask)
{
    CHK_PRT_RET(
        target_die_id >= ccu_max_iodie_num,
        HCCL_ERROR(
            "[CcuKernel][%s] failed, dieId[%u] should be less than [%u].", __func__, target_die_id, ccu_max_iodie_num),
        HcclResult::HCCL_E_PARA);

    CHK_PRT_RET(
        (valid_die_mask & (1U << target_die_id)) == 0,
        HCCL_ERROR(
            "[CcuKernel][%s] failed, target dieId[%u] is disabled, validDieMask[0x%x].", __func__, target_die_id,
            valid_die_mask),
        HcclResult::HCCL_E_PARA);
    CHK_RET(check_channels_die(channels_, target_die_id));

    set_die_id(target_die_id);
    move_resources_to_die(res_, target_die_id);
    (void)update_profiling_info(profiling_info, target_die_id, name_);
    return HcclResult::HCCL_SUCCESS;
}

void ccu_kernel::set_ins_generater(ccu_ins_generater_base* ins_generater_base) { ins_generator_ = ins_generater_base; }

CcuResult ccu_kernel::validate_task_args(const uint64_t* task_args, uint32_t args_num) const
{
    if (load_arg_used_set_.size() != args_num) {
        HCCL_ERROR(
            "[CcuKernel][%s] failed, args number does not match the Load instruction, "
            "argsNum = %u, loaded = %zu",
            __func__, args_num, load_arg_used_set_.size());
        return CcuResult::CCU_E_INTERNAL;
    }
    for (uint32_t i = 0; i < args_num; ++i) {
        if (load_arg_used_set_.count(i) == 0) {
            HCCL_ERROR("[CcuKernel][%s] failed, argId %u not loaded (argsNum=%u)", __func__, i, args_num);
            return CcuResult::CCU_E_INTERNAL;
        }
    }
    if (args_num != 0) {
        CCU_CHK_PTR_NULL(task_args);
    }
    if (instr_info_.mission_instr_count == 0 || instr_info_.instr_vec.empty()) {
        HCCL_ERROR(
            "[CcuKernel][%s] failed, mission instructions are empty, "
            "the kernel is not been translated yet.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

void ccu_kernel::fill_task_param(
    ccu_task_param& param, uint32_t index, uint32_t seq_num, const uint64_t* task_args, uint32_t args_num) const
{
    param.die_id = get_die_id();
    param.mission_id = get_mission_id();
    param.inst_start_id = instr_info_.mission_start_instr_id + index * ccu_sqe_args_len;
    param.key = get_mission_key();
    param.arg_size = ccu_sqe_args_len;

    const uint32_t pre_mission_ins_cnt = index * ccu_sqe_args_len;
    const bool is_last = (index == seq_num - 1);
    param.inst_cnt = is_last ? (instr_info_.mission_instr_count - pre_mission_ins_cnt) : ccu_sqe_args_len;

    if (args_num > pre_mission_ins_cnt) {
        const uint32_t args_to_copy =
            is_last ? std::min(args_num - pre_mission_ins_cnt, ccu_sqe_args_len) : ccu_sqe_args_len;
        std::copy(
            task_args + pre_mission_ins_cnt, task_args + pre_mission_ins_cnt + args_to_copy, std::begin(param.args));
    }

    HCCL_INFO(
        "[GeneTaskParam]task Param, dieId[%u] missionId[%u] instStartId[%u] instCnt[%u], argSize[%u]", param.die_id,
        param.mission_id, param.inst_start_id, param.inst_cnt, param.arg_size);
}

CcuResult ccu_kernel::gene_task_params(
    const uint64_t* task_args, uint32_t args_num, std::vector<ccu_task_param>& task_params)
{
    CCU_CHK_RET(validate_task_args(task_args, args_num));

    // 如果args数量超过sqe arg的最大数量，则返回多个TaskParam，前面几个只从sqe中加载args;
    // args数量大于等于0、小于等于最大值时，返回1个TaskParam
    const uint32_t seq_num =
        (args_num / ccu_sqe_args_len) + ((args_num % ccu_sqe_args_len) == 0 ? 0 : 1) + (args_num == 0 ? 1 : 0);

    const uint32_t pre_misson_sqe_ins_cnt = (seq_num - 1) * ccu_sqe_args_len;
    if (instr_info_.mission_instr_count < pre_misson_sqe_ins_cnt) {
        HCCL_ERROR(
            "[CcuKernel][%s] failed, missionInstrCount[%u] should be greater "
            "than preMissonSqeInsCnt[%u].",
            __func__, instr_info_.mission_instr_count, pre_misson_sqe_ins_cnt);
        return CcuResult::CCU_E_INTERNAL;
    }

    task_params.resize(seq_num);
    for (uint32_t index = 0; index < seq_num; index++) {
        fill_task_param(task_params[index], index, seq_num, task_args, args_num);
    }

    return CcuResult::CCU_SUCCESS;
}

HcclResult ccu_kernel::create_variable(const ChannelHandle channel, uint32_t var_index, ccu_rep::variable* var)
{
    channels_.insert(channel);

    ccu_channel channel_impl(channel);
    CHK_RET(channel_impl.get_result());
    uint32_t loc_xn_id{0};
    CHK_RET(channel_impl->get_loc_xn_by_index(var_index, loc_xn_id));
    var->reset(loc_xn_id, channel_impl->get_die_id());
    return HcclResult::HCCL_SUCCESS;
}

ccu_rep_resource& ccu_kernel::get_resource() { return res_; }

asc_ccu_res_request ccu_kernel::get_resource_request()
{
    asc_ccu_res_request req{};
    uint32_t die_id = get_die_id();
    req.count[HCOMM_CCU_BATCH_RES_MS][die_id] = res_.ccubufs[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][die_id] = res_.block_ccubufs[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_CKE][die_id] = res_.completed_event[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][die_id] =
        res_.block_completed_event[die_id].size() + res_.local_notify[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_LOOP][die_id] = res_.executor[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][die_id] = res_.block_executor[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_GSA][die_id] = res_.address[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][die_id] = res_.block_address[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_XN][die_id] = res_.variable[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][die_id] = res_.continuous_variable[die_id].size();
    req.count[HCOMM_CCU_BATCH_RES_MISSION][die_id] = 1;

    auto info = asc::format_ccu_message(
        "resource request: dieId[%u], ms[%u], blockMs[%u], cke[%u], blockCke[%u], "
        "loopEngine[%u], blockLoopEngine[%u], gsa[%u], blockGsa[%u], xn[%u], blockXn[%u], missionId[%u]",
        die_id, req.count[HCOMM_CCU_BATCH_RES_MS][die_id], req.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][die_id],
        req.count[HCOMM_CCU_BATCH_RES_CKE][die_id], req.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][die_id],
        req.count[HCOMM_CCU_BATCH_RES_LOOP][die_id], req.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][die_id],
        req.count[HCOMM_CCU_BATCH_RES_GSA][die_id], req.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][die_id],
        req.count[HCOMM_CCU_BATCH_RES_XN][die_id], req.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][die_id],
        req.count[HCOMM_CCU_BATCH_RES_MISSION][die_id]);

    HCCL_INFO("%s", info.c_str());

    return req;
}

template <typename handle_type, typename resource_type_t>
static CcuResult get_resource_by_handle(
    std::unordered_map<handle_type, resource_type_t>& resource_map, handle_type handle, resource_type_t** resource,
    const char* resource_type)
{
    auto iter = resource_map.find(handle);
    if (iter == resource_map.end()) {
        HCCL_ERROR("[%s] failed to find %s by handle: 0x%llx", __func__, resource_type, handle);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    // ccu资源本身可能重载=，对象赋值会被转换成指令，导致流程失败
    *resource = &(iter->second);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::get_variable_by_handle(ccu_variable_handle var_handle, ccu_rep::variable** variable)
{
    return get_resource_by_handle(ccu_var_map_, var_handle, variable, "variable");
}
// Alloc 相关接口
CcuResult ccu_kernel::variable_alloc(ccu_variable_handle* var_handle)
{
    HCCL_INFO("[VariableAlloc]");
    const auto& var = create_res_assist(res_.continuous_variable);
    ccu_variable_handle handle = ccu_var_map_.size();
    ccu_var_map_.emplace(handle, var);

    *var_handle = handle;
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_kernel::address_alloc(ccu_address_handle* addr_handle)
{
    HCCL_INFO("[AddressAlloc]");
    const auto addr = create_address();
    ccu_address_handle handle = ccu_addr_map_.size();
    ccu_addr_map_.emplace(handle, addr);
    *addr_handle = handle;
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_kernel::event_alloc(ccu_event_handle* event_handle)
{
    HCCL_INFO("[EventAlloc]");
    const auto& event = create_res_assist(res_.block_completed_event);
    ccu_event_handle handle = ccu_event_map_.size();
    ccu_event_map_.emplace(handle, event);
    *event_handle = handle;
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_kernel::buffer_alloc(ccu_buffer_handle* buf_handle)
{
    HCCL_INFO("[BufferAlloc]");
    const auto& buf = create_res_assist(res_.block_ccubufs);
    ccu_buffer_handle handle = ccu_buffer_map_.size();
    ccu_buffer_map_.emplace(handle, buf);
    *buf_handle = handle;
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_kernel::local_addr_alloc(
    ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    HCCL_INFO("[LocalAddrAlloc]");
    auto local_addr = create_local_addr();

    ccu_address_handle a_handle = ccu_addr_map_.size();
    ccu_addr_map_.emplace(a_handle, local_addr.addr);

    ccu_variable_handle t_handle = ccu_var_map_.size();
    ccu_var_map_.emplace(t_handle, local_addr.token);

    ccu_local_addr_handle la_handle = ccu_local_addr_map_.size();
    ccu_local_addr_map_.emplace(la_handle, local_addr);

    *local_addr_handle = la_handle;
    *addr_handle = a_handle;
    *token_handle = t_handle;
    return CcuResult::CCU_SUCCESS;
}
CcuResult ccu_kernel::remote_addr_alloc(
    ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle)
{
    HCCL_INFO("[RemoteAddrAlloc]");
    auto remote_addr = create_remote_addr();

    ccu_address_handle a_handle = ccu_addr_map_.size();
    ccu_addr_map_.emplace(a_handle, remote_addr.addr);

    ccu_variable_handle t_handle = ccu_var_map_.size();
    ccu_var_map_.emplace(t_handle, remote_addr.token);

    ccu_remote_addr_handle ra_handle = ccu_remote_addr_map_.size();
    ccu_remote_addr_map_.emplace(ra_handle, remote_addr);

    *remote_addr_handle = ra_handle;
    *addr_handle = a_handle;
    *token_handle = t_handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count)
{
    HCCL_INFO("[BlockVariableAlloc] count=%u", count);
    const auto& var = create_block_res_assist(count, res_.continuous_variable);
    for (uint32_t i = 0; i < count; i++) {
        ccu_variable_handle handle = ccu_var_map_.size();
        ccu_var_map_.emplace(handle, var[i]);
        var_handles[i] = handle;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::block_event_alloc(ccu_event_handle* event_handles, uint32_t count)
{
    HCCL_INFO("[BlockEventAlloc] count=%u", count);
    const auto& event = create_block_res_assist(count, res_.block_completed_event);
    for (uint32_t i = 0; i < count; i++) {
        ccu_event_handle handle = ccu_event_map_.size();
        ccu_event_map_.emplace(handle, event[i]);
        event_handles[i] = handle;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count)
{
    HCCL_INFO("[BlockBufferAlloc] count=%u", count);
    const auto& buffer = create_block_res_assist(count, res_.block_ccubufs);
    for (uint32_t i = 0; i < count; i++) {
        ccu_buffer_handle handle = ccu_buffer_map_.size();
        ccu_buffer_map_.emplace(handle, buffer[i]);
        buf_handles[i] = handle;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_create_by_channel(
    ChannelHandle channel, uint32_t var_index, ccu_variable_handle* var_handle)
{
    HCCL_INFO("[VariableCreateByChannel] channel=%llu, varIndex=%u", channel, var_index);
    channels_.insert(channel);
    ccu_rep::variable var(this);
    CCU_CHK_RET(create_variable(channel, var_index, &var));
    ccu_variable_handle handle = ccu_var_map_.size();
    ccu_var_map_.emplace(handle, var);
    *var_handle = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_create_by_acquire(
    ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle)
{
    uint8_t die_id = 0;
    uint32_t xn_id = 0;
    CCU_CHK_RET(ccu_var_event_res_mgr::get_instance(asc::get_current_ccu_device_logic_id())
                    .get_variable_xn_id(acq_handle, index, die_id, xn_id));

    ccu_rep::variable var(this);
    var.reset(static_cast<uint16_t>(xn_id), static_cast<uint16_t>(die_id));
    ccu_variable_handle handle = ccu_var_map_.size();
    ccu_var_map_.emplace(handle, var);
    *var_handle = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::event_create_by_acquire(
    ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle)
{
    uint8_t die_id = 0;
    uint32_t cke_id = 0;
    CCU_CHK_RET(ccu_var_event_res_mgr::get_instance(asc::get_current_ccu_device_logic_id())
                    .get_event_cke_id(acq_handle, index, die_id, cke_id));

    ccu_rep::completed_event event(this);
    event.reset(static_cast<uint16_t>(cke_id), static_cast<uint16_t>(die_id));
    ccu_event_handle handle = ccu_event_map_.size();
    ccu_event_map_.emplace(handle, event);
    *event_handle = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_assign_imm(ccu_variable_handle var_handle, uint64_t immediate)
{
    HCCL_INFO("[VariableAssignImm] varHandle=%llu, immediate=%llu", var_handle, immediate);
    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));
    // 通过符号重载实现，内部记录rep；异常由入口 asccomm_ccu_kernel_register 的
    // CCU_EXCEPTION_HANDLE_BEGIN/END 统一接住，无需在此局部 try/catch。
    (*variable) = immediate;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_assign_var(ccu_variable_handle var_handle, ccu_variable_handle var_a)
{
    HCCL_INFO("[VariableAssignVar] varHandle=%llu, varA=%llu", var_handle, var_a);
    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));
    ccu_rep::variable* variable_a{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_a, &variable_a));
    // 通过符号重载实现，内部记录rep；异常由入口统一 catch。
    (*variable) = (*variable_a);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_add_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    HCCL_INFO(
        "[VariableAddVarToVar] varHandle=%llu, varAHandle=%llu, varBHandle=%llu", var_handle, var_a_handle,
        var_b_handle);
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    // 通过符号重载实现，内部记录rep；异常由入口统一 catch。
    *res_var = *left_var + *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_sub_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var - *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_mul_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var * *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_add_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));

    *res_var = *left_var + immediate;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_sub_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));

    *res_var = *left_var - immediate;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_mul_imm_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));

    *res_var = *left_var * immediate;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_and_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var & *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_or_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var | *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_xor_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var ^ *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_not_var(ccu_variable_handle var_handle, ccu_variable_handle var_a_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));

    *res_var = ~(*left_var);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_shl_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var << *right_var;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::variable_shr_var_to_var(
    ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle)
{
    ccu_rep::variable* res_var{nullptr};
    ccu_rep::variable* left_var{nullptr};
    ccu_rep::variable* right_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &res_var));
    CCU_CHK_RET(get_variable_by_handle(var_a_handle, &left_var));
    CCU_CHK_RET(get_variable_by_handle(var_b_handle, &right_var));

    *res_var = *left_var >> *right_var;
    return CcuResult::CCU_SUCCESS;
}

/* ========== Event信号同步类 相关接口 ========== */
CcuResult ccu_kernel::event_record(ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO("[EventRecord] eventHandle=%llu, mask=%u", event_handle, mask);
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    // 复用已有的 RecordEvent 实现（内部 append CcuRepLocRecordEvent）
    CCU_CHK_RET(record_event(*event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::event_wait(ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO("[EventWait] eventHandle=%llu, mask=%u", event_handle, mask);
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    // 复用已有的 WaitEvent 实现（内部 append CcuRepLocWaitEvent）
    CCU_CHK_RET(wait_event(*event, mask));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::local_notify_record(const char* notify_tag, const uint32_t mask)
{
    HCCL_INFO("[LocalNotifyRecord] tag=%s, mask=%u", (notify_tag ? notify_tag : "null"), mask);
    if (notify_tag == nullptr) {
        HCCL_ERROR("[CcuKernel][%s] notifyTag is nullptr, please check.", __func__);
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel][%s] is not supported in loop block, please check.", __func__);
        return latch_body_error(CcuResult::CCU_E_NOT_SUPPORT);
    }

    const std::string tag_key(notify_tag);

    auto& shared_notifies = imported_res_.shared_notifies;
    if (shared_notifies.find(tag_key) == shared_notifies.end()) {
        ccu_rep::local_notify local_notify;
        shared_notifies.insert({tag_key, local_notify});
    }

    append(std::make_shared<ccu_rep::ccu_rep_record_shared_notify>(ins_generator_, shared_notifies.at(tag_key), mask));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::local_notify_wait(const char* notify_tag, const uint32_t mask)
{
    HCCL_INFO("[LocalNotifyWait] tag=%s, mask=%u", (notify_tag ? notify_tag : "null"), mask);
    if (notify_tag == nullptr) {
        HCCL_ERROR("[CcuKernel][%s] notifyTag is nullptr, please check.", __func__);
        return CcuResult::CCU_E_PTR;
    }

    const std::string tag_key(notify_tag);

    auto& shared_notifies = exported_res_.shared_notifies;
    if (shared_notifies.find(tag_key) == shared_notifies.end()) {
        ccu_rep::local_notify notify = create_local_notify();
        exported_res_.shared_notifies.insert({tag_key, notify});
    }

    bool is_profiling = current_block()->type() != ccu_rep::ccu_rep_type::loop_block;
    append(std::make_shared<ccu_rep::ccu_rep_loc_wait_notify>(
        ins_generator_, exported_res_.shared_notifies.at(tag_key), mask, is_profiling));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::notify_record(const ChannelHandle channel, uint32_t remote_notify_idx, uint32_t mask)
{
    HCCL_INFO("[NotifyRecord] channel=%llu, remoteNotifyIdx=%u, mask=%u", channel, remote_notify_idx, mask);
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[%s] NotifyRecord is not allowed inside a ccu::Loop body", __func__);
        return latch_body_error(CcuResult::CCU_E_NOT_SUPPORT);
    }
    channels_.insert(channel);
    append(std::make_shared<ccu_rep::ccu_rep_rem_post_sem>(ins_generator_, channel, remote_notify_idx, mask));
    return CCU_SUCCESS;
}

CcuResult ccu_kernel::notify_wait(const ChannelHandle channel, uint32_t local_notify_idx, uint32_t mask)
{
    HCCL_INFO("[NotifyWait] channel=%llu, localNotifyIdx=%u, mask=%u", channel, local_notify_idx, mask);
    channels_.insert(channel);
    bool is_profiling = current_block()->type() != ccu_rep::ccu_rep_type::loop_block;
    if (is_profiling) {
        CCU_CHK_RET(static_cast<HcclResult>(add_profiling(channel, "NotifyWait", local_notify_idx, mask)));
    }
    append(
        std::make_shared<ccu_rep::ccu_rep_rem_wait_sem>(ins_generator_, channel, local_notify_idx, mask, is_profiling));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::write_variable_with_notify(
    const ChannelHandle channel, ccu_variable_handle var_handle, uint32_t remote_var_idx, uint32_t remote_notify_idx,
    uint32_t mask)
{
    HCCL_INFO(
        "[WriteVariableWithNotify] channel=%llu, varHandle=%llu, remoteVarIdx=%u,"
        " remoteNotifyIdx=%u, mask=%u",
        channel, var_handle, remote_var_idx, remote_notify_idx, mask);
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[%s] WriteVariableWithNotify is not allowed inside a ccu::Loop body", __func__);
        return latch_body_error(CcuResult::CCU_E_NOT_SUPPORT);
    }
    channels_.insert(channel);
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    append(std::make_shared<ccu_rep::ccu_rep_rem_post_var>(
        ins_generator_, *var, channel, remote_var_idx, remote_notify_idx, mask));
    return CcuResult::CCU_SUCCESS;
}

// 加载类 相关接口
CcuResult ccu_kernel::load_arg(ccu_variable_handle var_handle, uint32_t arg_id)
{
    HCCL_INFO("[LoadArg] varHandle=%llu, argId=%u", var_handle, arg_id);
    load_arg_used_set_.insert(arg_id);
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    auto load_arg_rep = std::make_shared<ccu_rep::ccu_rep_load_arg>(
        ins_generator_, *var, arg_id % ccu_sqe_args_len, static_cast<uint16_t>(arg_id));
    append(load_arg_rep);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::get_ccu_kernel_info(ccu_kernel_info& info) const
{
    if (load_arg_used_set_.empty()) {
        info.max_task_args_num = 0;
        return CcuResult::CCU_SUCCESS;
    }

    uint32_t max_task_args_num = 0;
    for (const auto& arg_id : load_arg_used_set_) {
        max_task_args_num = std::max(max_task_args_num, arg_id);
    }
    info.max_task_args_num = max_task_args_num + 1;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::check_continuous_variables(
    ccu_variable_handle var_handle, uint32_t num, const ccu_rep::variable& base_var, const char* tag)
{
    if (num <= 1) {
        return CcuResult::CCU_SUCCESS;
    }
    for (uint32_t i = 1; i < num; i++) {
        ccu_rep::variable* next_var{nullptr};
        CCU_CHK_RET(get_variable_by_handle(var_handle + i, &next_var));
        if (next_var->id() != base_var.id() + i) {
            HCCL_ERROR(
                "[CcuKernel][%s] variables not continuous at index %u, "
                "expected Id %u but got %u",
                tag, i, base_var.id() + i, next_var->id());
            return HCCL_TO_CCU_RET(HCCL_E_PARA);
        }
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::load_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    HCCL_INFO("[LoadVar] addr=0x%llx, varHandle=%llu, num=%u", addr, var_handle, num);
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    CCU_CHK_RET(check_continuous_variables(var_handle, num, *var, "LoadVariable"));
    append(std::make_shared<ccu_rep::ccu_rep_load>(ins_generator_, addr, *var, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::ccu_load_var_from_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    HCCL_INFO(
        "[CcuLoadVarFromVarAddr] addrHandle=%llu, varHandle=%llu,"
        " num=%u",
        addr_handle, var_handle, num);
    ccu_rep::variable* addr_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(addr_handle, &addr_var));
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    CCU_CHK_RET(check_continuous_variables(var_handle, num, *var, "LoadVar dst"));
    append(std::make_shared<ccu_rep::ccu_rep_load_var>(ins_generator_, *addr_var, *var, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::store_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num)
{
    HCCL_INFO("[StoreVar] addr=0x%llx, varHandle=%llu, num=%u", addr, var_handle, num);
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    CCU_CHK_RET(check_continuous_variables(var_handle, num, *var, "StoreVariable"));
    append(std::make_shared<ccu_rep::ccu_rep_store>(ins_generator_, *var, addr, num));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::ccu_store_var_to_var_addr(
    ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num)
{
    HCCL_INFO(
        "[CcuStoreVarToVarAddr] addrHandle=%llu, varHandle=%llu,"
        " num=%u",
        addr_handle, var_handle, num);
    ccu_rep::variable* addr_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(addr_handle, &addr_var));
    ccu_rep::variable* var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &var));
    CCU_CHK_RET(check_continuous_variables(var_handle, num, *var, "StoreVar src"));
    append(std::make_shared<ccu_rep::ccu_rep_store_var>(ins_generator_, *var, *addr_var, num));
    return CcuResult::CCU_SUCCESS;
}

// 本地数据拷贝 相关实现
CcuResult ccu_kernel::local_copy_mem_to_buffer(
    ccu_buffer_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[LocalCopyMemToBuffer] dstHandle=%llu, srcHandle=%llu, lenHandle=%llu,"
        " eventHandle=%llu, mask=%u",
        dst_handle, src_handle, len_handle, event_handle, mask);
    ccu_rep::ccu_buf* dst{nullptr};
    CCU_CHK_RET(get_buffer_by_handle(dst_handle, &dst));
    ccu_rep::local_addr* src{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(src_handle, &src));
    ccu_rep::variable* len{nullptr};
    CCU_CHK_RET(get_variable_by_handle(len_handle, &len));
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    auto ret = local_copy_nb(*dst, *src, *len, *event, mask); // 复用 protected
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::local_copy_buffer_to_mem(
    ccu_local_addr_handle dst_handle, ccu_buffer_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[LocalCopyBufferToMem] dstHandle=%llu, srcHandle=%llu, lenHandle=%llu,"
        " eventHandle=%llu, mask=%u",
        dst_handle, src_handle, len_handle, event_handle, mask);
    ccu_rep::local_addr* dst{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(dst_handle, &dst));
    ccu_rep::ccu_buf* src{nullptr};
    CCU_CHK_RET(get_buffer_by_handle(src_handle, &src));
    ccu_rep::variable* len{nullptr};
    CCU_CHK_RET(get_variable_by_handle(len_handle, &len));
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    auto ret = local_copy_nb(*dst, *src, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::local_copy_mem_to_mem(
    ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[LocalCopyMemToMem] dstHandle=%llu, srcHandle=%llu, lenHandle=%llu,"
        " eventHandle=%llu, mask=%u",
        dst_handle, src_handle, len_handle, event_handle, mask);
    ccu_rep::local_addr* dst{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(dst_handle, &dst));
    ccu_rep::local_addr* src{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(src_handle, &src));
    ccu_rep::variable* len{nullptr};
    CCU_CHK_RET(get_variable_by_handle(len_handle, &len));
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    auto ret = local_copy_nb(*dst, *src, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

// 本地reduce 相关实现
CcuResult ccu_kernel::local_mem_reduce(
    ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
    HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[LocalMemReduce] dstHandle=%llu, srcHandle=%llu, lenHandle=%llu, dataType=%d,"
        " op=%d, eventHandle=%llu, mask=%u",
        dst_handle, src_handle, len_handle, data_type, op_type, event_handle, mask);
    ccu_rep::local_addr* dst{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(dst_handle, &dst));
    ccu_rep::local_addr* src{nullptr};
    CCU_CHK_RET(get_local_addr_by_handle(src_handle, &src));
    ccu_rep::variable* len{nullptr};
    CCU_CHK_RET(get_variable_by_handle(len_handle, &len));
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    auto ret = local_reduce_nb(*dst, *src, *len, data_type, op_type, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::local_buffer_reduce(
    ccu_buffer_handle* buf_handles, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[LocalBufferReduce] count=%u, dataType=%d, outDataType=%d,"
        " op=%d, lenHandle=%llu, eventHandle=%llu, mask=%u",
        count, data_type, output_data_type, op_type, len_handle, event_handle, mask);
    ccu_rep::variable* len{nullptr};
    CCU_CHK_RET(get_variable_by_handle(len_handle, &len));
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(get_event_by_handle(event_handle, &event));
    std::vector<ccu_rep::ccu_buf> bufs(count);
    for (uint32_t i = 0; i < count; i++) {
        ccu_rep::ccu_buf* buf{nullptr};
        CCU_CHK_RET(get_buffer_by_handle(buf_handles[i], &buf));
        bufs[i] = *buf;
    }
    auto ret = local_reduce_nb(bufs.data(), count, data_type, output_data_type, op_type, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

/* ========== 远端数据传输操作 ========== */

CcuResult ccu_kernel::resolve_buf_remote_len_event(
    ccu_buffer_handle buf_handle, ccu_remote_addr_handle remote_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, ccu_rep::ccu_buf** buf, ccu_rep::remote_addr** remote, ccu_rep::variable** len,
    ccu_rep::completed_event** event)
{
    CCU_CHK_RET(get_buffer_by_handle(buf_handle, buf));
    CCU_CHK_RET(get_remote_addr_by_handle(remote_handle, remote));
    CCU_CHK_RET(get_variable_by_handle(len_handle, len));
    CCU_CHK_RET(get_event_by_handle(event_handle, event));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::resolve_local_remote_len_event(
    ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, ccu_rep::local_addr** local, ccu_rep::remote_addr** remote, ccu_rep::variable** len,
    ccu_rep::completed_event** event)
{
    CCU_CHK_RET(get_local_addr_by_handle(local_handle, local));
    CCU_CHK_RET(get_remote_addr_by_handle(remote_handle, remote));
    CCU_CHK_RET(get_variable_by_handle(len_handle, len));
    CCU_CHK_RET(get_event_by_handle(event_handle, event));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::resolve_remote_local_len_event(
    ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle, ccu_variable_handle len_handle,
    ccu_event_handle event_handle, ccu_rep::remote_addr** remote, ccu_rep::local_addr** local, ccu_rep::variable** len,
    ccu_rep::completed_event** event)
{
    CCU_CHK_RET(get_remote_addr_by_handle(remote_handle, remote));
    CCU_CHK_RET(get_local_addr_by_handle(local_handle, local));
    CCU_CHK_RET(get_variable_by_handle(len_handle, len));
    CCU_CHK_RET(get_event_by_handle(event_handle, event));
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::read_mem_to_mem(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[ReadMemToMem] channel=%llu, localHandle=%llu, remoteHandle=%llu,"
        " lenHandle=%llu, eventHandle=%llu, mask=%u",
        channel, local_handle, remote_handle, len_handle, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::local_addr* local{nullptr};
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_local_remote_len_event(
        local_handle, remote_handle, len_handle, event_handle, &local, &remote, &len, &event));
    auto ret = read_nb(channel, *local, *remote, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::read_mem_to_buffer(
    ChannelHandle channel, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[ReadMemToBuffer] channel=%llu, bufHandle=%llu, remoteHandle=%llu,"
        " lenHandle=%llu, eventHandle=%llu, mask=%u",
        channel, local_handle, remote_handle, len_handle, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::ccu_buf* local{nullptr};
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_buf_remote_len_event(
        local_handle, remote_handle, len_handle, event_handle, &local, &remote, &len, &event));
    auto ret = read_nb(channel, *local, *remote, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::read_mem_to_mem_reduce(
    ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
    ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
    uint32_t mask)
{
    HCCL_INFO(
        "[ReadMemToMemReduce] channel=%llu, lenHandle=%llu, dataType=%d, op=%d,"
        " eventHandle=%llu, mask=%u",
        channel, len_handle, data_type, op_type, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::local_addr* local{nullptr};
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_local_remote_len_event(
        local_handle, remote_handle, len_handle, event_handle, &local, &remote, &len, &event));
    auto ret = read_reduce_nb(channel, *local, *remote, *len, data_type, op_type, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::write_mem_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[WriteMemToMem] channel=%llu, remoteHandle=%llu, localHandle=%llu,"
        " lenHandle=%llu, eventHandle=%llu, mask=%u",
        channel, remote_handle, local_handle, len_handle, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::local_addr* local{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_remote_local_len_event(
        remote_handle, local_handle, len_handle, event_handle, &remote, &local, &len, &event));
    auto ret = write_nb(channel, *remote, *local, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::write_buffer_to_mem(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_buffer_handle local_handle,
    ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask)
{
    HCCL_INFO(
        "[WriteBufferToMem] channel=%llu, remoteHandle=%llu, bufHandle=%llu,"
        " lenHandle=%llu, eventHandle=%llu, mask=%u",
        channel, remote_handle, local_handle, len_handle, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::ccu_buf* local{nullptr};
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_buf_remote_len_event(
        local_handle, remote_handle, len_handle, event_handle, &local, &remote, &len, &event));
    auto ret = write_nb(channel, *remote, *local, *len, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

CcuResult ccu_kernel::write_mem_to_mem_reduce(
    ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
    ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
    uint32_t mask)
{
    HCCL_INFO(
        "[WriteMemToMemReduce] channel=%llu, lenHandle=%llu, dataType=%d, op=%d,"
        " eventHandle=%llu, mask=%u",
        channel, len_handle, data_type, op_type, event_handle, mask);
    channels_.insert(channel);
    ccu_rep::remote_addr* remote{nullptr};
    ccu_rep::local_addr* local{nullptr};
    ccu_rep::variable* len{nullptr};
    ccu_rep::completed_event* event{nullptr};
    CCU_CHK_RET(resolve_remote_local_len_event(
        remote_handle, local_handle, len_handle, event_handle, &remote, &local, &len, &event));
    auto ret = write_reduce_nb(channel, *remote, *local, *len, data_type, op_type, *event, mask);
    return HCCL_TO_CCU_RET(ret);
}

void ccu_kernel::flush_closable_pending_ifs()
{
    if (is_flushing_) {
        return;
    }
    is_flushing_ = true;
    while (if_label_stack_top_is_closable()) {
        const char* lbl = if_label_stack_pop();
        if (lbl != nullptr) {
            if_end(lbl);
        }
    }
    is_flushing_ = false;
}

void ccu_kernel::append(std::shared_ptr<ccu_rep::ccu_rep_base> rep)
{
    flush_closable_pending_ifs();
    ccu_rep::ccu_rep_context::append(rep);
}

namespace {
std::shared_ptr<ccu_rep::ccu_rep_jump_base> make_inverted_cond_jump_imm(
    ccu_ins_generater_base* ins_generator, const std::string& dest_label_str, const ccu_rep::variable& target_var,
    const ccu_rep::variable& expect_var, const ccu_rep::variable& variable, uint64_t immediate,
    ccu_condition_type cond_type, const char* func_name)
{
    switch (cond_type) {
        case ccu_condition_eq:
            return std::make_shared<ccu_rep::ccu_rep_jump_ne>(
                ins_generator, dest_label_str, target_var, expect_var, variable, immediate);
        case ccu_condition_ne:
            return std::make_shared<ccu_rep::ccu_rep_jump_eq>(
                ins_generator, dest_label_str, target_var, expect_var, variable, immediate);
        default:
            HCCL_ERROR("[%s] unsupported condition type: %d", func_name, cond_type);
            return nullptr;
    }
}

// 双变量版本：当 (lhsVar OP rhsVar) 为假时跳转到 destLabelStr。
std::shared_ptr<ccu_rep::ccu_rep_jump_base> make_inverted_cond_jump_var(
    ccu_ins_generater_base* ins_generator, const std::string& dest_label_str, const ccu_rep::variable& target_var,
    const ccu_rep::variable& lhs_var, const ccu_rep::variable& rhs_var, ccu_condition_type cond_type,
    const char* func_name)
{
    switch (cond_type) {
        case ccu_condition_eq:
            return std::make_shared<ccu_rep::ccu_rep_jump_ne>(
                ins_generator, dest_label_str, target_var, lhs_var, rhs_var);
        case ccu_condition_ne:
            return std::make_shared<ccu_rep::ccu_rep_jump_eq>(
                ins_generator, dest_label_str, target_var, lhs_var, rhs_var);
        default:
            HCCL_ERROR("[%s] unsupported condition type: %d", func_name, cond_type);
            return nullptr;
    }
}
} // namespace

CcuResult ccu_kernel::if_begin(
    ccu_variable_handle var_handle, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    HCCL_INFO(
        "[IfBegin] varHandle=%llu, immediate=%llu, condType=%d, label=%s", var_handle, immediate, cond_type,
        (label ? label : "null"));
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_IF is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));

    flush_closable_pending_ifs();

    std::string label_str(label);
    if (pending_if_ctx_.find(label_str) != pending_if_ctx_.end()) {
        HCCL_ERROR("[%s] label '%s' already has a pending IfBegin without IfEnd", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    std::string else_label_str = label_str + "_else";
    std::string end_label_str = label_str + "_end";
    auto else_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, else_label_str);
    auto end_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, end_label_str);
    auto target_var = ccu_rep::create_variable(this);
    auto expect_var = create_expect_var();

    // 反转条件："if <cond>, 执行块" 等价于 "!<cond> 时跳过块"。
    auto jump = make_inverted_cond_jump_imm(
        ins_generator_, else_label_str, target_var, expect_var, *variable, immediate, cond_type, __func__);
    if (jump == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    jump->reference(else_label);
    append(jump);

    pending_if_context ctx;
    ctx.else_label = else_label;
    ctx.end_label = end_label;
    ctx.has_else = false;
    pending_if_ctx_.emplace(label_str, std::move(ctx));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::if_begin_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_IF is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    ccu_rep::variable* lhs_var{nullptr};
    ccu_rep::variable* rhs_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(lhs_handle, &lhs_var));
    CCU_CHK_RET(get_variable_by_handle(rhs_handle, &rhs_var));

    flush_closable_pending_ifs();

    std::string label_str(label);
    if (pending_if_ctx_.find(label_str) != pending_if_ctx_.end()) {
        HCCL_ERROR("[%s] label '%s' already has a pending IfBegin without IfEnd", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    std::string end_label_str = label_str + "_end";
    std::string else_label_str = label_str + "_else";
    auto end_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, end_label_str);
    auto else_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, else_label_str);
    auto target_var = ccu_rep::create_variable(this);

    auto jump = make_inverted_cond_jump_var(
        ins_generator_, else_label_str, target_var, *lhs_var, *rhs_var, cond_type, __func__);
    if (jump == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    jump->reference(else_label);
    append(jump);

    pending_if_context ctx;
    ctx.has_else = false;
    ctx.end_label = end_label;
    ctx.else_label = else_label;
    pending_if_ctx_.emplace(label_str, std::move(ctx));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::if_else(const char* label)
{
    HCCL_INFO("[IfElse] label=%s", (label ? label : "null"));
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_ELSE is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    std::string label_str(label);
    auto iter = pending_if_ctx_.find(label_str);
    if (iter == pending_if_ctx_.end()) {
        HCCL_ERROR("[%s] no matching IfBegin for label '%s'", __func__, label);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    if (iter->second.has_else) {
        HCCL_ERROR("[%s] label '%s' already has an IfElse", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    // At end of then-block: unconditional jump past else-block to endLabel
    std::string end_label_str = label_str + "_end";
    auto skip_else_var = ccu_rep::create_variable(this);
    auto skip_else_jump = std::make_shared<ccu_rep::ccu_rep_jump>(ins_generator_, end_label_str, skip_else_var);
    skip_else_jump->reference(iter->second.end_label);
    append(skip_else_jump);

    // Place the else label (entry point of else-block)
    append(iter->second.else_label);

    iter->second.has_else = true;

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::if_end(const char* label)
{
    HCCL_INFO("[IfEnd] label=%s", (label ? label : "null"));
    std::string label_str(label);
    auto iter = pending_if_ctx_.find(label_str);
    if (iter == pending_if_ctx_.end()) {
        HCCL_ERROR("[%s] no matching IfBegin for label '%s'", __func__, label);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    if (iter->second.has_else) {
        // Had else-block: place endLabel after else-block
        append(iter->second.end_label);
    } else {
        // No else-block: place elseLabel as the skip target
        append(iter->second.else_label);
    }

    pending_if_ctx_.erase(iter);

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::while_begin(
    ccu_variable_handle var_handle, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    HCCL_INFO(
        "[WhileBegin] varHandle=%llu, immediate=%llu, condType=%d, label=%s", var_handle, immediate, cond_type,
        (label ? label : "null"));
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_WHILE is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));

    std::string label_str(label);
    if (pending_while_ctx_.find(label_str) != pending_while_ctx_.end()) {
        HCCL_ERROR("[%s] label '%s' already has a pending WhileBegin without WhileEnd", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    std::string begin_label_str = label_str + "_begin";
    std::string end_label_str = label_str + "_end";
    auto begin_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, begin_label_str);
    auto end_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, end_label_str);

    append(begin_label);

    auto target_var = ccu_rep::create_variable(this);
    auto expect_var = create_expect_var();
    auto jump = make_inverted_cond_jump_imm(
        ins_generator_, end_label_str, target_var, expect_var, *variable, immediate, cond_type, __func__);
    if (jump == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    jump->reference(end_label);
    append(jump);

    pending_while_context ctx;
    ctx.begin_label = begin_label;
    ctx.end_label = end_label;
    ctx.var_handle = var_handle;
    ctx.immediate = immediate;
    ctx.cond_type = cond_type;
    pending_while_ctx_.emplace(label_str, std::move(ctx));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::while_begin_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_WHILE is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    ccu_rep::variable* lhs_var{nullptr};
    ccu_rep::variable* rhs_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(lhs_handle, &lhs_var));
    CCU_CHK_RET(get_variable_by_handle(rhs_handle, &rhs_var));

    std::string label_str(label);
    if (pending_while_ctx_.find(label_str) != pending_while_ctx_.end()) {
        HCCL_ERROR("[%s] label '%s' already has a pending WhileBegin without WhileEnd", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    std::string end_label_str = label_str + "_end";
    std::string begin_label_str = label_str + "_begin";
    auto end_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, end_label_str);
    auto begin_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, begin_label_str);

    append(begin_label);

    auto target_var = ccu_rep::create_variable(this);
    auto jump =
        make_inverted_cond_jump_var(ins_generator_, end_label_str, target_var, *lhs_var, *rhs_var, cond_type, __func__);
    if (jump == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    jump->reference(end_label);
    append(jump);

    pending_while_context ctx;
    ctx.begin_label = begin_label;
    ctx.end_label = end_label;
    ctx.var_handle = lhs_handle;
    ctx.immediate = 0;
    ctx.cond_type = cond_type;
    pending_while_ctx_.emplace(label_str, std::move(ctx));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::while_end(const char* label)
{
    HCCL_INFO("[WhileEnd] label=%s", (label ? label : "null"));
    std::string label_str(label);
    auto iter = pending_while_ctx_.find(label_str);
    if (iter == pending_while_ctx_.end()) {
        HCCL_ERROR("[%s] no matching WhileBegin for label '%s'", __func__, label);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    std::string begin_label_str = label_str + "_begin";
    auto loop_back_var = ccu_rep::create_variable(this);
    auto loop_back_jump = std::make_shared<ccu_rep::ccu_rep_jump>(ins_generator_, begin_label_str, loop_back_var);
    loop_back_jump->reference(iter->second.begin_label);
    append(loop_back_jump);

    append(iter->second.end_label);

    pending_while_ctx_.erase(iter);

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::do_while_begin(const char* label)
{
    HCCL_INFO("[DoWhileBegin] label=%s", (label ? label : "null"));
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR(
            "[%s] CCU_DO is not allowed inside a ccu::Loop body (label='%s')", __func__,
            label != nullptr ? label : "(null)");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    std::string label_str(label);
    if (pending_do_while_ctx_.find(label_str) != pending_do_while_ctx_.end()) {
        HCCL_ERROR("[%s] label '%s' already has a pending DoWhileBegin without DoWhileEnd", __func__, label);
        return CcuResult::CCU_E_PARA;
    }

    std::string begin_label_str = label_str + "_begin";
    auto begin_label = std::make_shared<ccu_rep::ccu_rep_jump_label>(ins_generator_, begin_label_str);
    append(begin_label);

    pending_do_while_context ctx;
    ctx.begin_label = begin_label;
    pending_do_while_ctx_.emplace(label_str, std::move(ctx));

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::do_while_end(
    ccu_variable_handle var_handle, uint64_t immediate, ccu_condition_type cond_type, const char* label)
{
    HCCL_INFO(
        "[DoWhileEnd] varHandle=%llu, immediate=%llu, condType=%d, label=%s", var_handle, immediate, cond_type,
        (label ? label : "null"));
    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));

    std::string label_str(label);
    auto iter = pending_do_while_ctx_.find(label_str);
    if (iter == pending_do_while_ctx_.end()) {
        HCCL_ERROR("[%s] no matching DoWhileBegin for label '%s'", __func__, label);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    std::string begin_label_str = label_str + "_begin";
    auto target_var = ccu_rep::create_variable(this);
    auto expect_var = create_expect_var();
    std::shared_ptr<ccu_rep::ccu_rep_jump_base> jump{nullptr};

    // "condition true => continue looping" means jump back to begin when condition holds
    if (cond_type == ccu_condition_eq) {
        jump = std::make_shared<ccu_rep::ccu_rep_jump_eq>(
            ins_generator_, begin_label_str, target_var, expect_var, *variable, immediate);
    } else if (cond_type == ccu_condition_ne) {
        jump = std::make_shared<ccu_rep::ccu_rep_jump_ne>(
            ins_generator_, begin_label_str, target_var, expect_var, *variable, immediate);
    } else {
        HCCL_ERROR("[%s] unsupported condition type: %d", __func__, cond_type);
        return CcuResult::CCU_E_PARA;
    }

    jump->reference(iter->second.begin_label);
    append(jump);

    pending_do_while_ctx_.erase(iter);

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::do_while_end_var(
    ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type, const char* label)
{
    ccu_rep::variable* lhs_var{nullptr};
    ccu_rep::variable* rhs_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(lhs_handle, &lhs_var));
    CCU_CHK_RET(get_variable_by_handle(rhs_handle, &rhs_var));

    std::string label_str(label);
    auto iter = pending_do_while_ctx_.find(label_str);
    if (iter == pending_do_while_ctx_.end()) {
        HCCL_ERROR("[%s] no matching DoWhileBegin for label '%s'", __func__, label);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    std::string begin_label_str = label_str + "_begin";
    auto target_var = ccu_rep::create_variable(this);
    std::shared_ptr<ccu_rep::ccu_rep_jump_base> jump{nullptr};

    if (cond_type == ccu_condition_eq) {
        jump =
            std::make_shared<ccu_rep::ccu_rep_jump_eq>(ins_generator_, begin_label_str, target_var, *lhs_var, *rhs_var);
    } else if (cond_type == ccu_condition_ne) {
        jump =
            std::make_shared<ccu_rep::ccu_rep_jump_ne>(ins_generator_, begin_label_str, target_var, *lhs_var, *rhs_var);
    } else {
        HCCL_ERROR("[%s] unsupported condition type: %d", __func__, cond_type);
        return CcuResult::CCU_E_PARA;
    }

    jump->reference(iter->second.begin_label);
    append(jump);

    pending_do_while_ctx_.erase(iter);

    return CcuResult::CCU_SUCCESS;
}

// 控制流标签栈实体
void ccu_kernel::if_label_stack_push(const char* label) { iflabel_stack_.push_back({label, false}); }
void ccu_kernel::if_label_stack_mark_body_done()
{
    if (iflabel_stack_.empty()) {
        HCCL_ERROR("[CcuKernel::IfLabelStack][MarkBodyDone] stack is empty");
        return;
    }
    iflabel_stack_.back().body_done = true;
}
const char* ccu_kernel::if_label_stack_pop_for_else()
{
    if (iflabel_stack_.empty()) {
        HCCL_ERROR("[CcuKernel::IfLabelStack][PopForElse] orphan CCU_ELSE: "
                   "no matching CCU_IF on the stack");
        return nullptr;
    }
    if (!iflabel_stack_.back().body_done) {
        HCCL_ERROR(
            "[CcuKernel::IfLabelStack][PopForElse] CCU_ELSE called while "
            "top if-body is still InBody (label='%s')",
            iflabel_stack_.back().label != nullptr ? iflabel_stack_.back().label : "(null)");
        return nullptr;
    }
    const char* label = iflabel_stack_.back().label;
    iflabel_stack_.pop_back();
    return label;
}
bool ccu_kernel::if_label_stack_top_is_closable() { return !iflabel_stack_.empty() && iflabel_stack_.back().body_done; }

const char* ccu_kernel::if_label_stack_pop()
{
    if (iflabel_stack_.empty()) {
        return nullptr;
    }
    const char* label = iflabel_stack_.back().label;
    iflabel_stack_.pop_back();
    return label;
}

void ccu_kernel::do_while_label_stack_push(const char* label)
{
    do_while_label_entry entry;
    entry.label = label;
    entry.snapshot_block = current_block();
    entry.snapshot_rep_count = (entry.snapshot_block != nullptr) ? entry.snapshot_block->get_reps().size() : 0;
    do_while_label_stack_.push_back(std::move(entry));
}

const char* ccu_kernel::do_while_label_stack_pop_for_while()
{
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        return nullptr;
    }
    if (do_while_label_stack_.empty()) {
        return nullptr;
    }
    do_while_label_entry entry = do_while_label_stack_.back();
    do_while_label_stack_.pop_back();

    auto current_block_ptr = current_block();
    size_t current_rep_count = (current_block_ptr != nullptr) ? current_block_ptr->get_reps().size() : 0;
    if (current_block_ptr != entry.snapshot_block || current_rep_count != entry.snapshot_rep_count) {
        HCCL_ERROR(
            "[CcuKernel::DoWhileLabelStackPopForWhile] dangling CCU calls between CCU_DO end "
            "and CCU_WHILE (label='%s', snapRep=%zu, curRep=%zu, blockChanged=%d); they must "
            "be syntactically adjacent, otherwise the code in between is pulled into the body.",
            entry.label != nullptr ? entry.label : "(null)", entry.snapshot_rep_count, current_rep_count,
            current_block_ptr != entry.snapshot_block ? 1 : 0);
        return nullptr;
    }
    return entry.label;
}

CcuResult ccu_kernel::get_address_by_handle(ccu_address_handle addr_handle, ccu_rep::address** address)
{
    return get_resource_by_handle(ccu_addr_map_, addr_handle, address, "address");
}

// addr = 立即数 → CcuRepAssign(Address, uint64_t)
CcuResult ccu_kernel::address_assign_imm(ccu_address_handle addr_handle, uint64_t immediate)
{
    HCCL_INFO("[AddressAssignImm] addrHandle=%llu, immediate=%llu", addr_handle, immediate);
    ccu_rep::address* address{nullptr};
    CCU_CHK_RET(get_address_by_handle(addr_handle, &address));
    (*address) = immediate;
    return CcuResult::CCU_SUCCESS;
}

// addr = variable → CcuRepAssign(Address, Variable)
CcuResult ccu_kernel::address_assign_var(ccu_address_handle addr_handle, ccu_variable_handle var_handle)
{
    HCCL_INFO("[AddressAssignVar] addrHandle=%llu, varHandle=%llu", addr_handle, var_handle);
    ccu_rep::address* address{nullptr};
    CCU_CHK_RET(get_address_by_handle(addr_handle, &address));

    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));

    (*address) = (*variable);
    return CcuResult::CCU_SUCCESS;
}
// addr = addr → CcuRepAssign(Address, Address)
CcuResult ccu_kernel::address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle)
{
    HCCL_INFO("[AddressAssignAddr] dstAddrHandle=%llu, srcAddrHandle=%llu", dst_addr_handle, src_addr_handle);
    ccu_rep::address* dst_address{nullptr};
    CCU_CHK_RET(get_address_by_handle(dst_addr_handle, &dst_address));

    ccu_rep::address* src_address{nullptr};
    CCU_CHK_RET(get_address_by_handle(src_addr_handle, &src_address));

    (*dst_address) = (*src_address);
    return CcuResult::CCU_SUCCESS;
}

// resAddr = lhsAddr + rhsVar → CcuRepAdd(Address, Address, Variable)
CcuResult ccu_kernel::address_add_var_to_addr(
    ccu_address_handle res_addr_handle, ccu_address_handle lhs_addr_handle, ccu_variable_handle rhs_var_handle)
{
    HCCL_INFO(
        "[AddressAddVarToAddr] resAddrHandle=%llu, lhsAddrHandle=%llu, rhsVarHandle=%llu", res_addr_handle,
        lhs_addr_handle, rhs_var_handle);
    ccu_rep::address* res_addr{nullptr};
    ccu_rep::address* lhs_addr{nullptr};
    CCU_CHK_RET(get_address_by_handle(res_addr_handle, &res_addr));
    CCU_CHK_RET(get_address_by_handle(lhs_addr_handle, &lhs_addr));

    ccu_rep::variable* rhs_var{nullptr};
    CCU_CHK_RET(get_variable_by_handle(rhs_var_handle, &rhs_var));

    *res_addr = *lhs_addr + *rhs_var;
    return CcuResult::CCU_SUCCESS;
}

// resAddr = addrA + addrB → CcuRepAdd(Address, Address, Address)
CcuResult ccu_kernel::address_add_addr_to_addr(
    ccu_address_handle res_addr_handle, ccu_address_handle addr_a_handle, ccu_address_handle addr_b_handle)
{
    HCCL_INFO(
        "[AddressAddAddrToAddr] resAddrHandle=%llu, addrAHandle=%llu, addrBHandle=%llu", res_addr_handle, addr_a_handle,
        addr_b_handle);
    ccu_rep::address* res_addr{nullptr};
    ccu_rep::address* addr_a{nullptr};
    ccu_rep::address* addr_b{nullptr};
    CCU_CHK_RET(get_address_by_handle(res_addr_handle, &res_addr));
    CCU_CHK_RET(get_address_by_handle(addr_a_handle, &addr_a));
    CCU_CHK_RET(get_address_by_handle(addr_b_handle, &addr_b));

    *res_addr = *addr_a + *addr_b;
    return CcuResult::CCU_SUCCESS;
}

// addr += variable → CcuRepAdd(Address, Variable) 就地加
CcuResult ccu_kernel::address_add_assign_var(ccu_address_handle addr_handle, ccu_variable_handle var_handle)
{
    HCCL_INFO("[AddressAddAssignVar] addrHandle=%llu, varHandle=%llu", addr_handle, var_handle);
    ccu_rep::address* address{nullptr};
    CCU_CHK_RET(get_address_by_handle(addr_handle, &address));

    ccu_rep::variable* variable{nullptr};
    CCU_CHK_RET(get_variable_by_handle(var_handle, &variable));

    (*address) += (*variable);
    return CcuResult::CCU_SUCCESS;
}

// addr += addr → 等价于 addr = addr + otherAddr
CcuResult ccu_kernel::address_add_assign_addr(ccu_address_handle addr_handle, ccu_address_handle other_handle)
{
    HCCL_INFO("[AddressAddAssignAddr] addrHandle=%llu, otherHandle=%llu", addr_handle, other_handle);
    ccu_rep::address* address{nullptr};
    CCU_CHK_RET(get_address_by_handle(addr_handle, &address));

    ccu_rep::address* other{nullptr};
    CCU_CHK_RET(get_address_by_handle(other_handle, &other));

    (*address) = (*address) + (*other);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::address_add_imm_to_addr(
    ccu_address_handle res_addr_handle, ccu_address_handle addr_a_handle, uint16_t imm)
{
    ccu_rep::address* res_addr{nullptr};
    ccu_rep::address* addr_a{nullptr};
    CCU_CHK_RET(get_address_by_handle(res_addr_handle, &res_addr));
    CCU_CHK_RET(get_address_by_handle(addr_a_handle, &addr_a));

    *res_addr = *addr_a + imm;
    return CcuResult::CCU_SUCCESS;
}

void ccu_kernel::load(const ccu_rep::variable& var)
{
    auto load_arg_rep = std::make_shared<ccu_rep::ccu_rep_load_arg>(
        ins_generator_, var, load_arg_index_ % ccu_sqe_args_len, static_cast<uint16_t>(load_arg_index_));
    get_lg_profiling_info().load_rep2_arg_idx_map[load_arg_rep] = load_arg_index_;
    append(load_arg_rep);
    load_arg_index_++;
}

void ccu_kernel::store_variable(const ccu_rep::variable& var, uint64_t addr)
{
    append(std::make_shared<ccu_rep::ccu_rep_store>(ins_generator_, var, addr));
}

void ccu_kernel::load_variable(const ccu_rep::variable& src, const ccu_rep::variable& var)
{
    append(std::make_shared<ccu_rep::ccu_rep_load_var>(ins_generator_, src, var));
}

HcclResult ccu_kernel::record_event(ccu_rep::completed_event event, uint32_t mask)
{
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel][%s] is not supported in loop block, please check.", __func__);
        latch_body_error(HCCL_TO_CCU_RET(HcclResult::HCCL_E_NOT_SUPPORT));
        return HcclResult::HCCL_E_NOT_SUPPORT;
    }

    auto rep = std::make_shared<ccu_rep::ccu_rep_loc_record_event>(ins_generator_, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::wait_event(ccu_rep::completed_event event, uint32_t mask)
{
    bool is_profiling = current_block()->type() != ccu_rep::ccu_rep_type::loop_block;
    auto rep = std::make_shared<ccu_rep::ccu_rep_loc_wait_event>(ins_generator_, event, mask, is_profiling);
    if (is_profiling) {
        CHK_RET(static_cast<HcclResult>(add_profiling("WaitEvent", rep->get_mask())));
    }
    rep->set_dependency_info(get_dependency_info(event.id()));
    erase_dependency_info(event.id());
    append(rep);
    return HCCL_SUCCESS;
}

CcuResult ccu_kernel::get_event_by_handle(ccu_event_handle event_handle, ccu_rep::completed_event** event)
{
    return get_resource_by_handle(ccu_event_map_, event_handle, event, "completedEvent");
}

/*
LocalAddr / RemoteAddr 相关接口
*/
CcuResult ccu_kernel::get_local_addr_by_handle(ccu_local_addr_handle handle, ccu_rep::local_addr** local_addr)
{
    return get_resource_by_handle(ccu_local_addr_map_, handle, local_addr, "localAddr");
}

CcuResult ccu_kernel::get_remote_addr_by_handle(ccu_remote_addr_handle handle, ccu_rep::remote_addr** remote_addr)
{
    return get_resource_by_handle(ccu_remote_addr_map_, handle, remote_addr, "remoteAddr");
}

/* Read新接口 */
HcclResult ccu_kernel::read_nb(
    const ChannelHandle channel, const ccu_rep::ccu_buf& loc, const ccu_rep::remote_addr& rem,
    const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_buf_read>(ins_generator_, channel, rem, loc, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

/* Write新接口 */
HcclResult ccu_kernel::write_nb(
    const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::ccu_buf& loc,
    const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_buf_write>(ins_generator_, channel, loc, rem, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

static bool is_low_precision_in(HcclDataType data_type)
{
    return data_type == HCCL_DATA_TYPE_INT8 || data_type == HCCL_DATA_TYPE_HIF8 ||
           data_type == HCCL_DATA_TYPE_FP8E4M3 || data_type == HCCL_DATA_TYPE_FP8E5M2;
}

static bool is_low_precision_out(HcclDataType data_type)
{
    return data_type == HCCL_DATA_TYPE_FP16 || data_type == HCCL_DATA_TYPE_BFP16 || data_type == HCCL_DATA_TYPE_FP32;
}

HcclResult ccu_kernel::local_reduce_nb(
    const ccu_rep::ccu_buf* bufs, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask)
{
    if ((op_type == HCCL_REDUCE_SUM && is_low_precision_in(data_type) && !is_low_precision_out(output_data_type)) ||
        (op_type == HCCL_REDUCE_SUM && !is_low_precision_in(data_type) && data_type != output_data_type) ||
        (op_type != HCCL_REDUCE_SUM && data_type != output_data_type)) {
        return HCCL_E_NOT_SUPPORT;
    }

    std::vector<ccu_rep::ccu_buf> ccu_bufs(count);
    for (uint32_t i = 0; i < count; i++) {
        ccu_bufs[i] = bufs[i];
    }

    auto rep = std::make_shared<ccu_rep::ccu_rep_buf_reduce>(
        ins_generator_, ccu_bufs, count, ccu_rep::get_ccu_data_type(data_type, op_type),
        ccu_rep::get_ccu_data_type(output_data_type, op_type), ccu_rep::get_ccu_reduce_type(op_type), event, len, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

/* Read新接口 */
HcclResult ccu_kernel::read_nb(
    const ChannelHandle channel, const ccu_rep::local_addr& loc, const ccu_rep::remote_addr& rem,
    const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_read>(ins_generator_, channel, loc, rem, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

/* ReadReduce新接口 */
HcclResult ccu_kernel::read_reduce_nb(
    const ChannelHandle channel, const ccu_rep::local_addr& loc, const ccu_rep::remote_addr& rem,
    const ccu_rep::variable& len, HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event,
    uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_read>(
        ins_generator_, channel, loc, rem, len, ccu_rep::get_ub_data_type(data_type),
        ccu_rep::get_ub_reduce_type(op_type), event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

/* Write新接口 */
HcclResult ccu_kernel::write_nb(
    const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::local_addr& loc,
    const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_write>(ins_generator_, channel, rem, loc, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::write_reduce_nb(
    const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::local_addr& loc,
    const ccu_rep::variable& len, HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event,
    uint32_t mask)
{
    channels_.insert(channel);
    auto rep = std::make_shared<ccu_rep::ccu_rep_write>(
        ins_generator_, channel, rem, loc, len, ccu_rep::get_ub_data_type(data_type),
        ccu_rep::get_ub_reduce_type(op_type), event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

CcuResult ccu_kernel::get_buffer_by_handle(ccu_buffer_handle buffer_handle, ccu_rep::ccu_buf** buffer)
{
    return get_resource_by_handle(ccu_buffer_map_, buffer_handle, buffer, "buffer");
}

HcclResult ccu_kernel::local_copy_nb(
    const ccu_rep::local_addr& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
    ccu_rep::completed_event event, uint32_t mask)
{
    auto rep = std::make_shared<ccu_rep::ccu_rep_loc_cpy>(ins_generator_, dst, src, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::local_copy_nb(
    const ccu_rep::ccu_buf& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
    ccu_rep::completed_event event, uint32_t mask)
{
    auto rep = std::make_shared<ccu_rep::ccu_rep_buf_loc_read>(ins_generator_, src, dst, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::local_copy_nb(
    const ccu_rep::local_addr& dst, const ccu_rep::ccu_buf& src, const ccu_rep::variable& len,
    ccu_rep::completed_event event, uint32_t mask)
{
    auto rep = std::make_shared<ccu_rep::ccu_rep_buf_loc_write>(ins_generator_, src, dst, len, event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::local_reduce_nb(
    const ccu_rep::local_addr& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
    HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event, uint32_t mask)
{
    auto rep = std::make_shared<ccu_rep::ccu_rep_loc_cpy>(
        ins_generator_, dst, src, len, ccu_rep::get_ub_data_type(data_type), ccu_rep::get_ub_reduce_type(op_type),
        event, mask);
    append(rep);
    set_dependency_info(event.id(), mask, rep);
    return HCCL_SUCCESS;
}

ccu_rep::func_call ccu_kernel::func(const std::string& label) { return ccu_rep::func_call(this, label); }

ccu_rep::func_call ccu_kernel::func(const ccu_rep::variable& func_addr) { return ccu_rep::func_call(this, func_addr); }

ccu_rep::loop_call ccu_kernel::loop(const std::string& label) { return ccu_rep::loop_call(this, label); }

CcuResult ccu_kernel::loop_create(ccu_loop* loop)
{
    HCCL_INFO("[LoopCreate]");
    if (loop == nullptr) {
        HCCL_ERROR("[CcuKernel::LoopCreate] null pointer");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel::LoopCreate] cannot create loop inside a loop body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    if (in_func_body_) {
        HCCL_ERROR("[CcuKernel::LoopCreate] cannot create loop inside a func body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    ccu_loop handle = ++loop_handle_counter_;
    std::string label = "loop_" + std::to_string(handle);

    loop_descriptor desc;
    desc.label = label;
    desc.rep_loop_block = std::make_shared<ccu_rep::ccu_rep_loop_block>(ins_generator_, label);

    loop_map_[handle] = std::move(desc);
    *loop = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::latch_body_error(CcuResult err)
{
    if ((current_block()->type() == ccu_rep::ccu_rep_type::loop_block || in_func_body_) &&
        body_error_ == CcuResult::CCU_SUCCESS) {
        body_error_ = err;
    }
    return err;
}

CcuResult ccu_kernel::loop_body_enter(ccu_loop loop)
{
    HCCL_INFO("[LoopBodyEnter] loop=%llu", loop);
    auto it = loop_map_.find(loop);
    if (it == loop_map_.end()) {
        HCCL_ERROR("[CcuKernel::LoopBodyEnter] invalid loop handle %lu", loop);
        return CcuResult::CCU_E_PARA;
    }
    auto& desc = it->second;
    if (desc.body_defined) {
        HCCL_ERROR("[CcuKernel::LoopBodyEnter] loop %lu body already defined", loop);
        return CcuResult::CCU_E_INTERNAL;
    }

    append(desc.rep_loop_block);
    desc.prev_active_block = current_block();
    set_current_block(desc.rep_loop_block);
    ++loop_body_depth_;
    body_error_ = CcuResult::CCU_SUCCESS;

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_body_exit(ccu_loop loop)
{
    HCCL_INFO("[LoopBodyExit] loop=%llu", loop);
    auto it = loop_map_.find(loop);
    if (it == loop_map_.end()) {
        HCCL_ERROR("[CcuKernel::LoopBodyExit] invalid loop handle %lu", loop);
        return CcuResult::CCU_E_PARA;
    }
    auto& desc = it->second;

    set_current_block(desc.prev_active_block);
    desc.body_defined = true;
    --loop_body_depth_;

    if (body_error_ != CcuResult::CCU_SUCCESS) {
        const CcuResult err = body_error_;
        HCCL_ERROR("[CcuKernel::LoopBodyExit] illegal operation inside loop body, err=%d", err);
        body_error_ = CcuResult::CCU_SUCCESS;
        return err;
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::func_block_lookup(const void* func_ptr, uint64_t* out_handle)
{
    HCCL_INFO("[FuncBlockLookup] funcPtr=%p", func_ptr);
    if (func_ptr == nullptr || out_handle == nullptr) {
        HCCL_ERROR("[CcuKernel::FuncBlockLookup] null pointer");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block || in_func_body_) {
        HCCL_ERROR("[CcuKernel::FuncBlockLookup] ccu::CallFunc only allowed at top level");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    auto it = func_instance_map_.find(func_ptr);
    *out_handle = (it == func_instance_map_.end()) ? 0 : it->second;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::func_block_begin(const void* func_ptr, uint64_t* out_handle)
{
    HCCL_INFO("[FuncBlockBegin] funcPtr=%p", func_ptr);
    if (func_ptr == nullptr || out_handle == nullptr) {
        HCCL_ERROR("[CcuKernel::FuncBlockBegin] null pointer");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block || in_func_body_) {
        HCCL_ERROR("[CcuKernel::FuncBlockBegin] ccu::CallFunc only allowed at top level");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    auto exist = func_instance_map_.find(func_ptr);
    if (exist != func_instance_map_.end()) {
        *out_handle = exist->second;
        return CcuResult::CCU_SUCCESS;
    }

    const uint64_t handle = ++func_handle_counter_;
    std::string label = "func_" + std::to_string(handle);

    func_descriptor desc;
    desc.func_ptr = func_ptr;
    desc.label = label;
    desc.rep_func_block = std::make_shared<ccu_rep::ccu_rep_func_block>(ins_generator_, label);
    desc.prev_active_block = current_block();

    func_map_[handle] = desc;
    set_current_block(desc.rep_func_block);
    in_func_body_ = true;
    body_error_ = CcuResult::CCU_SUCCESS;
    *out_handle = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::func_block_end(uint64_t handle)
{
    HCCL_INFO("[FuncBlockEnd] handle=%llu", handle);
    auto it = func_map_.find(handle);
    if (it == func_map_.end()) {
        HCCL_ERROR("[CcuKernel::FuncBlockEnd] invalid func handle %lu", handle);
        return CcuResult::CCU_E_PARA;
    }
    auto& desc = it->second;

    set_current_block(desc.prev_active_block);
    in_func_body_ = false;

    if (body_error_ != CcuResult::CCU_SUCCESS) {
        const CcuResult err = body_error_;
        HCCL_ERROR("[CcuKernel::FuncBlockEnd] illegal operation inside func body, err=%d", err);
        body_error_ = CcuResult::CCU_SUCCESS;
        func_map_.erase(it);
        return err;
    }

    append(desc.rep_func_block);
    desc.body_defined = true;
    func_instance_map_[desc.func_ptr] = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::func_define_in_arg(uint64_t handle, ccu_variable_handle formal)
{
    HCCL_INFO("[FuncDefineInArg] handle=%llu, formal=%llu", handle, formal);
    auto it = func_map_.find(handle);
    if (it == func_map_.end()) {
        HCCL_ERROR("[CcuKernel::FuncDefineInArg] invalid func handle %lu", handle);
        return CcuResult::CCU_E_PARA;
    }

    ccu_rep::variable* formal_var = nullptr;
    CCU_CHK_RET(get_variable_by_handle(formal, &formal_var));
    it->second.rep_func_block->define_in_arg(*formal_var);
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in)
{
    HCCL_INFO("[FuncCall] handle=%llu, numIn=%u", handle, num_in);
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block || in_func_body_) {
        HCCL_ERROR("[CcuKernel::FuncCall] ccu::CallFunc only allowed at top level");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    if (num_in > 0 && in_args == nullptr) {
        HCCL_ERROR("[CcuKernel::FuncCall] null input args");
        return CcuResult::CCU_E_PTR;
    }

    auto it = func_map_.find(handle);
    if (it == func_map_.end() || !it->second.body_defined) {
        HCCL_ERROR("[CcuKernel::FuncCall] invalid func handle %lu", handle);
        return CcuResult::CCU_E_PARA;
    }

    auto rep_func_call = std::make_shared<ccu_rep::ccu_rep_func_call>(ins_generator_, it->second.label);
    for (uint32_t i = 0; i < num_in; i++) {
        ccu_rep::variable* actual = nullptr;
        CCU_CHK_RET(get_variable_by_handle(in_args[i], &actual));
        rep_func_call->set_in_arg(*actual);
    }
    append(rep_func_call);
    return CcuResult::CCU_SUCCESS;
}

// 按 maxLoopNum 把 res_.blockExecutor[0] 扩容到至少 maxLoopNum 个 LoopEngine。
// 与 CreateBlockResAssist 对齐：所有 LoopEngine 资源先落在 die0 池，待
// 实际 die 确定后再由 MoveResourcesToDie 迁移到目标 die。
// 不同 LoopGroup 通过 local loopIdx 复用同一池低位 executorId，所以这里只
// "补足"而不是"累加"。
CcuResult ccu_kernel::ensure_loop_engine_pool(uint32_t max_loop_num)
{
    if (max_loop_num == 0) {
        HCCL_ERROR("[CcuKernel::EnsureLoopEnginePool] maxLoopNum must be > 0");
        return CcuResult::CCU_E_PARA;
    }
    const uint32_t max_pool_size =
        (ccu_version_ == HCOMM_CCU_VERSION_V2) ? max_loop_engine_pool_size_v2 : max_loop_engine_pool_size_v1;
    if (max_loop_num > max_pool_size) {
        HCCL_ERROR(
            "[CcuKernel::EnsureLoopEnginePool] maxLoopNum(%u) exceeds max supported %u", max_loop_num, max_pool_size);
        return CcuResult::CCU_E_PARA;
    }
    constexpr uint32_t pool_die_id = 0;
    auto& loop_engine_pool = res_.block_executor[pool_die_id];
    if (max_loop_num <= loop_engine_pool.size()) {
        return CcuResult::CCU_SUCCESS;
    }
    const uint32_t deficit = max_loop_num - static_cast<uint32_t>(loop_engine_pool.size());
    std::vector<ccu_rep::executor> tmp(deficit, ccu_rep::executor(this));
    (void)create_block_executor(deficit, tmp.data());
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_create(
    ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config)
{
    HCCL_INFO("[LoopGroupCreate] maxLoopNum=%u", max_loop_num);
    if (group == nullptr || config == nullptr) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreate] null pointer");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreate] cannot create loop group inside a loop body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    if (in_func_body_) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreate] cannot create loop group inside a func body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    // 按需扩 LoopEngine 池；池足够则复用低位 executorId，跨组共享。
    CCU_CHK_RET(ensure_loop_engine_pool(max_loop_num));

    ccu_loop_group handle = ++loop_group_handle_counter_;

    loop_group_descriptor desc;
    desc.config = *config;
    desc.parallel_var = create_variable();
    desc.offset_var = create_variable();
    desc.is_var_based = false;

    auto bundle = std::make_shared<ccu_rep::ccu_rep_loop_group_bundle>(
        ins_generator_, *config, desc.parallel_var, desc.offset_var);
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        bundle->set_xn_offset_var(create_variable());
    }
    desc.bundle_rep = bundle;
    append(bundle);

    loop_group_map_[handle] = std::move(desc);
    *group = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_create_from_var(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var_handle,
    ccu_variable_handle offset_var_handle)
{
    HCCL_INFO(
        "[LoopGroupCreateFromVar] maxLoopNum=%u, parallelVarHandle=%llu, offsetVarHandle=%llu", max_loop_num,
        parallel_var_handle, offset_var_handle);
    if (group == nullptr) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVar] null pointer for group");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVar] cannot create loop group inside a loop body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }
    if (in_func_body_) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVar] cannot create loop group inside a func body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    CCU_CHK_RET(ensure_loop_engine_pool(max_loop_num));

    ccu_rep::variable* parallel_var_ptr = nullptr;
    ccu_rep::variable* offset_var_ptr = nullptr;
    CCU_CHK_RET(get_variable_by_handle(parallel_var_handle, &parallel_var_ptr));
    CCU_CHK_RET(get_variable_by_handle(offset_var_handle, &offset_var_ptr));

    ccu_loop_group handle = ++loop_group_handle_counter_;

    loop_group_descriptor desc;
    desc.parallel_var = ccu_rep::variable(*parallel_var_ptr);
    desc.offset_var = ccu_rep::variable(*offset_var_ptr);
    desc.is_var_based = true;

    auto bundle =
        std::make_shared<ccu_rep::ccu_rep_loop_group_bundle>(ins_generator_, desc.parallel_var, desc.offset_var);
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        bundle->set_compat_remap_vars(create_variable(), create_variable());
    }
    desc.bundle_rep = bundle;
    append(bundle);

    loop_group_map_[handle] = std::move(desc);
    *group = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_create_from_var_v2(
    ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var_v2_handle,
    ccu_variable_handle offset_var_v2_handle, ccu_variable_handle var_offset_var_handle)
{
    if (ccu_version_ != HCOMM_CCU_VERSION_V2) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVarV2] only supported on V2");
        return CcuResult::CCU_E_NOT_SUPPORT;
    }
    if (group == nullptr) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVarV2] null pointer for group");
        return CcuResult::CCU_E_PTR;
    }
    if (current_block()->type() == ccu_rep::ccu_rep_type::loop_block) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVarV2] cannot create loop group inside a loop body");
        return CcuResult::CCU_E_INTERNAL;
    }
    if (in_func_body_) {
        HCCL_ERROR("[CcuKernel::LoopGroupCreateFromVar] cannot create loop group inside a func body");
        return latch_body_error(CcuResult::CCU_E_INTERNAL);
    }

    CCU_CHK_RET(ensure_loop_engine_pool(max_loop_num));

    ccu_rep::variable* parallel_var_ptr = nullptr;
    ccu_rep::variable* offset_var_ptr = nullptr;
    ccu_rep::variable* xn_offset_var_ptr = nullptr;
    CCU_CHK_RET(get_variable_by_handle(parallel_var_v2_handle, &parallel_var_ptr));
    CCU_CHK_RET(get_variable_by_handle(offset_var_v2_handle, &offset_var_ptr));
    CCU_CHK_RET(get_variable_by_handle(var_offset_var_handle, &xn_offset_var_ptr));

    ccu_loop_group handle = ++loop_group_handle_counter_;

    loop_group_descriptor desc;
    desc.parallel_var = ccu_rep::variable(*parallel_var_ptr);
    desc.offset_var = ccu_rep::variable(*offset_var_ptr);
    desc.xn_offset_var = ccu_rep::variable(*xn_offset_var_ptr);
    desc.is_var_based = true;
    desc.is_version_v2 = true;

    auto bundle =
        std::make_shared<ccu_rep::ccu_rep_loop_group_bundle>(ins_generator_, desc.parallel_var, desc.offset_var);
    bundle->set_layout(ccu_rep::ccu_rep_loop_group_bundle::layout::version_v2);
    bundle->set_xn_offset_var(desc.xn_offset_var);
    desc.bundle_rep = bundle;
    append(bundle);

    loop_group_map_[handle] = std::move(desc);
    *group = handle;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::lookup_loop_group_and_loop(
    ccu_loop_group group, ccu_loop loop, const char* fn_name, const char* create_fn_name,
    loop_group_descriptor*& grp_desc, loop_descriptor*& loop_desc, uint32_t& loop_idx)
{
    auto grp_it = loop_group_map_.find(group);
    if (grp_it == loop_group_map_.end()) {
        HCCL_ERROR("[CcuKernel::%s] invalid group handle %lu", fn_name, group);
        return CcuResult::CCU_E_PARA;
    }
    grp_desc = &grp_it->second;

    auto loop_it = loop_map_.find(loop);
    if (loop_it == loop_map_.end()) {
        HCCL_ERROR("[CcuKernel::%s] invalid loop handle %lu", fn_name, loop);
        return CcuResult::CCU_E_PARA;
    }
    loop_desc = &loop_it->second;

    if (!loop_desc->body_defined) {
        HCCL_ERROR("[CcuKernel::%s] loop %lu body not defined", fn_name, loop);
        return CcuResult::CCU_E_INTERNAL; // CCU_E_LOOP_BODY_UNDEFINED
    }

    auto& loop_engine_pool = res_.block_executor[0];
    loop_idx = grp_desc->loop_count;
    if (loop_idx >= loop_engine_pool.size()) {
        HCCL_ERROR(
            "[CcuKernel::%s] loopEngine pool exhausted (pool size %zu, loopIdx %u). "
            "Pass a larger maxLoopNum to %s so the pool can be extended at create time.",
            fn_name, loop_engine_pool.size(), loop_idx, create_fn_name);
        return CcuResult::CCU_E_PARA;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config)
{
    HCCL_INFO("[LoopGroupAddLoop] group=%llu, loop=%llu", group, loop);
    if (config == nullptr) {
        HCCL_ERROR("[CcuKernel::LoopGroupAddLoop] null pointer for config");
        return CcuResult::CCU_E_PTR;
    }

    loop_group_descriptor* grp_desc = nullptr;
    loop_descriptor* loop_desc = nullptr;
    uint32_t loop_idx = 0;
    CCU_CHK_RET(lookup_loop_group_and_loop(
        group, loop, "LoopGroupAddLoop", "CcuLoopGroupCreate", grp_desc, loop_desc, loop_idx));
    auto& loop_engine_pool = res_.block_executor[0];

    grp_desc->loop_count++;
    grp_desc->total_loop_num = grp_desc->loop_count;

    ccu_rep::ccu_rep_loop_group_bundle::loop_entry entry;
    entry.config = *config;
    entry.executor_value = loop_engine_pool[loop_idx];
    entry.rep_loop_block = loop_desc->rep_loop_block;
    entry.loop_param_var = create_variable();
    entry.layout_value = ccu_rep::ccu_rep_loop_group_bundle::layout::config;
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        entry.iter_num_var = create_variable();
        entry.addr_offset_var = create_variable();
        entry.ctx_id_var = create_variable();
    }

    auto bundle = std::static_pointer_cast<ccu_rep::ccu_rep_loop_group_bundle>(grp_desc->bundle_rep);
    bundle->add_loop(entry);

    if (!grp_desc->is_var_based) {
        bundle->set_repeat_loop_idx(grp_desc->config.clone_loop_offset);
        bundle->set_total_loop_num(grp_desc->total_loop_num);
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_add_loop_from_var(
    ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var_handle)
{
    HCCL_INFO(
        "[LoopGroupAddLoopFromVar] group=%llu, loop=%llu, loopParamVarHandle=%llu", group, loop, loop_param_var_handle);
    loop_group_descriptor* grp_desc = nullptr;
    loop_descriptor* loop_desc = nullptr;
    uint32_t loop_idx = 0;
    CCU_CHK_RET(lookup_loop_group_and_loop(
        group, loop, "LoopGroupAddLoopFromVar", "CcuLoopGroupCreateFromVar", grp_desc, loop_desc, loop_idx));
    auto& loop_engine_pool = res_.block_executor[0];

    ccu_rep::variable* loop_param_var_ptr = nullptr;
    CCU_CHK_RET(get_variable_by_handle(loop_param_var_handle, &loop_param_var_ptr));

    ccu_rep::ccu_rep_loop_group_bundle::loop_entry entry;
    entry.executor_value = loop_engine_pool[loop_idx];
    entry.rep_loop_block = loop_desc->rep_loop_block;
    entry.loop_param_var = ccu_rep::variable(*loop_param_var_ptr);
    entry.layout_value = ccu_rep::ccu_rep_loop_group_bundle::layout::packed_var;
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        entry.iter_num_var = create_variable();
        entry.addr_offset_var = create_variable();
        entry.ctx_id_var = create_variable();
    }

    auto bundle = std::static_pointer_cast<ccu_rep::ccu_rep_loop_group_bundle>(grp_desc->bundle_rep);
    bundle->add_loop(entry);

    // 计数放在入 bundle 之后统一更新：totalLoopNum 仅供 config 组同步编码使用，与 AddLoop 无先后依赖。
    grp_desc->loop_count++;
    grp_desc->total_loop_num = grp_desc->loop_count;
    if (!grp_desc->is_var_based) {
        bundle->set_repeat_loop_idx(grp_desc->config.clone_loop_offset);
        bundle->set_total_loop_num(grp_desc->total_loop_num);
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel::loop_group_add_loop_from_var_v2(
    ccu_loop_group group, ccu_loop loop, ccu_variable_handle iter_num_var_handle,
    ccu_variable_handle addr_offset_var_handle, ccu_variable_handle ctx_id_var_handle)
{
    if (ccu_version_ != HCOMM_CCU_VERSION_V2) {
        HCCL_ERROR("[CcuKernel::LoopGroupAddLoopFromVarV2] only supported on V2");
        return CcuResult::CCU_E_NOT_SUPPORT;
    }

    loop_group_descriptor* grp_desc = nullptr;
    loop_descriptor* loop_desc = nullptr;
    uint32_t loop_idx = 0;
    CCU_CHK_RET(lookup_loop_group_and_loop(
        group, loop, "LoopGroupAddLoopFromVarV2", "CcuLoopGroupCreateFromVarV2", grp_desc, loop_desc, loop_idx));
    auto& loop_engine_pool = res_.block_executor[0];

    ccu_rep::variable* iter_num_var_ptr = nullptr;
    ccu_rep::variable* addr_offset_var_ptr = nullptr;
    ccu_rep::variable* ctx_id_var_ptr = nullptr;
    CCU_CHK_RET(get_variable_by_handle(iter_num_var_handle, &iter_num_var_ptr));
    CCU_CHK_RET(get_variable_by_handle(addr_offset_var_handle, &addr_offset_var_ptr));
    CCU_CHK_RET(get_variable_by_handle(ctx_id_var_handle, &ctx_id_var_ptr));

    grp_desc->loop_count++;
    grp_desc->total_loop_num = grp_desc->loop_count;

    version_v2_loop_record record;
    record.iter_num_var = ccu_rep::variable(*iter_num_var_ptr);
    record.addr_offset_var = ccu_rep::variable(*addr_offset_var_ptr);
    record.ctx_id_var = ccu_rep::variable(*ctx_id_var_ptr);
    grp_desc->version_v2_loops.push_back(record);

    ccu_rep::ccu_rep_loop_group_bundle::loop_entry entry;
    entry.executor_value = loop_engine_pool[loop_idx];
    entry.rep_loop_block = loop_desc->rep_loop_block;
    entry.iter_num_var = ccu_rep::variable(*iter_num_var_ptr);
    entry.addr_offset_var = ccu_rep::variable(*addr_offset_var_ptr);
    entry.ctx_id_var = ccu_rep::variable(*ctx_id_var_ptr);
    entry.layout_value = ccu_rep::ccu_rep_loop_group_bundle::layout::version_v2;

    auto bundle = std::static_pointer_cast<ccu_rep::ccu_rep_loop_group_bundle>(grp_desc->bundle_rep);
    bundle->add_loop(entry);

    if (!grp_desc->is_var_based) {
        bundle->set_repeat_loop_idx(grp_desc->config.clone_loop_offset);
        bundle->set_total_loop_num(grp_desc->total_loop_num);
    }

    return CcuResult::CCU_SUCCESS;
}

void ccu_kernel::set_instr_id(uint32_t instr_id) { instr_info_.start_instr_id = instr_id; }

uint32_t ccu_kernel::get_instr_id() const { return instr_info_.start_instr_id; }

void ccu_kernel::set_instruction_resource(const HcommCcuResRangePod& range)
{
    instruction_resource_ = range;
    has_instruction_resource_ = true;
}

bool ccu_kernel::get_instruction_resource(HcommCcuResRangePod& range) const
{
    if (!has_instruction_resource_) {
        return false;
    }
    range = instruction_resource_;
    return true;
}

void ccu_kernel::clear_instruction_resource()
{
    instruction_resource_ = {};
    has_instruction_resource_ = false;
}

uint32_t ccu_kernel::get_instr_count()
{
    uint32_t instr_count = 0;
    for (const auto& rep : get_rep_sequence()) {
        instr_count += rep->instr_count();
    }
    instr_info_.instr_count = instr_count;
    HCCL_INFO("Kernel inst %u", instr_count);
    return instr_count;
}

namespace {
bool is_cke_wait_rep(const std::shared_ptr<ccu_rep::ccu_rep_base>& rep)
{
    if (rep == nullptr) {
        return false;
    }
    switch (rep->type()) {
        case ccu_rep::ccu_rep_type::loc_wait_event:
        case ccu_rep::ccu_rep_type::loc_wait_notify:
        case ccu_rep::ccu_rep_type::rem_wait_sem:
        case ccu_rep::ccu_rep_type::load:
        case ccu_rep::ccu_rep_type::load_var:
        case ccu_rep::ccu_rep_type::store:
        case ccu_rep::ccu_rep_type::store_var:
        case ccu_rep::ccu_rep_type::record_shared_notify:
            return true;
        default:
            return false;
    }
}

uint32_t count_cke_wait_rep_in_block(const std::shared_ptr<ccu_rep::ccu_rep_base>& rep)
{
    const auto type = rep->type();
    if (type != ccu_rep::ccu_rep_type::block && type != ccu_rep::ccu_rep_type::func_block &&
        type != ccu_rep::ccu_rep_type::loop_block) {
        return 0;
    }
    const auto* block = static_cast<const ccu_rep::ccu_rep_block*>(rep.get());
    uint32_t count = 0;
    for (const auto& child : const_cast<ccu_rep::ccu_rep_block*>(block)->get_reps()) {
        count += is_cke_wait_rep(child) ? 1U : 0U;
    }
    return count;
}
} // namespace

uint32_t ccu_kernel::get_rep_need_to_add_latency() const
{
    if (ccu_version_ != HCOMM_CCU_VERSION_V2) {
        return 0;
    }
    uint32_t count = 0;
    for (const auto& rep : const_cast<ccu_kernel*>(this)->get_rep_sequence()) {
        if (rep == nullptr) {
            continue;
        }
        count += is_cke_wait_rep(rep) ? 1U : 0U;
        count += count_cke_wait_rep_in_block(rep);
    }
    return count;
}

void ccu_kernel::set_ccu_instr_info(const ccu_rep::ccu_instr_info& instr_info) { this->instr_info_ = instr_info; }

ccu_rep::variable ccu_kernel::create_variable() { return create_res_assist(res_.continuous_variable); }

ccu_rep::variable ccu_kernel::create_expect_var()
{
    // v2(A6) jump compares two variables and needs a real XN for the immediate value.
    // v1(A5) compares with the immediate directly, so keep the placeholder out of the resource ledger.
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        return create_variable();
    }
    return ccu_rep::variable(this);
}

ccu_rep::address ccu_kernel::create_address()
{
    if (ccu_version_ == HCOMM_CCU_VERSION_V2) {
        // A6创建Address时，需要添加到Variable的列表中，但是仍以Address返回
        return ccu_rep::address(create_res_assist(res_.continuous_variable));
    }
    return create_res_assist(res_.block_address);
}

ccu_rep::local_notify ccu_kernel::create_local_notify() { return create_res_assist(res_.local_notify); }

ccu_rep::completed_event ccu_kernel::create_completed_event() { return create_res_assist(res_.block_completed_event); }

ccu_rep::ccu_buf ccu_kernel::create_ccu_buf() { return create_res_assist(res_.block_ccubufs); }

ccu_rep::executor ccu_kernel::create_executor() { return create_res_assist(res_.block_executor); }

ccu_rep::local_addr ccu_kernel::create_local_addr() { return ccu_rep::local_addr(create_address(), create_variable()); }

ccu_rep::remote_addr ccu_kernel::create_remote_addr()
{
    return ccu_rep::remote_addr(create_address(), create_variable());
}

ccu_rep::remote_addr ccu_kernel::get_remote_addr(const ChannelHandle channel, uint32_t index)
{
    (void)index;
    channels_.insert(channel);
    auto mem = ccu_rep::remote_addr(create_address(), create_variable());
    append(std::make_shared<ccu_rep::ccu_rep_rem_mem>(ins_generator_, channel, mem));
    return mem;
}

ccu_rep::local_addr ccu_kernel::create_local_addr(const ccu_rep::variable& token)
{
    return ccu_rep::local_addr(create_address(), token);
}

HcclResult ccu_kernel::create_block_ccu_buf(const uint32_t count, ccu_rep::ccu_buf* ccu_bufs)
{
    CHK_PTR_NULL(ccu_bufs);
    auto resources = create_block_res_assist(count, res_.block_ccubufs);

    for (uint32_t i = 0; i < count; i++) {
        ccu_bufs[i] = resources[i]; // 拷贝虚拟资源，通过shared_ptr链接到物理资源
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel::create_block_executor(const uint32_t count, ccu_rep::executor* ccu_exes)
{
    CHK_PTR_NULL(ccu_exes);
    auto resources = create_block_res_assist(count, res_.block_executor);

    for (uint32_t i = 0; i < count; i++) {
        ccu_exes[i] = resources[i]; // 拷贝虚拟资源，通过shared_ptr链接到物理资源
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel::create_block_completed_event(const uint32_t count, ccu_rep::completed_event* ccu_events)
{
    CHK_PTR_NULL(ccu_events);
    auto resources = create_block_res_assist(count, res_.block_completed_event);

    for (uint32_t i = 0; i < count; i++) {
        ccu_events[i] = resources[i]; // 拷贝虚拟资源，通过shared_ptr链接到物理资源
    }

    return HcclResult::HCCL_SUCCESS;
}

void ccu_kernel::set_res_repository(const asc_ccu_res_repository& res_repo) { res_repo_ = res_repo; }

void ccu_kernel::set_res_repository(asc_ccu_res_repository&& res_repo) { res_repo_ = std::move(res_repo); }

asc_ccu_res_repository& ccu_kernel::get_res_repository() { return res_repo_; }

ccu_shared_resource& ccu_kernel::get_exported_res() { return exported_res_; }

ccu_shared_resource& ccu_kernel::get_imported_res() { return imported_res_; }

static HcclResult get_arg_index(
    const std::unordered_map<uint16_t, uint16_t>& var_id2_var_id_map,
    const std::unordered_map<uint16_t, uint32_t>& var_id2_arg_index_map, const uint64_t* task_args, uint32_t arg_size,
    uint16_t var_id, uint64_t& arg_index)
{
    HCCL_INFO("[GetArgIndex] Enter varId(%u)", var_id);
    auto item = var_id2_arg_index_map.find(var_id);
    if (item == var_id2_arg_index_map.end()) {
        uint16_t ori_var_id = var_id;
        auto iter = var_id2_var_id_map.find(var_id);
        while (iter != var_id2_var_id_map.end()) { // 循环查找中间assign Rep，找到起始varId
            ori_var_id = iter->second;
            iter = var_id2_var_id_map.find(ori_var_id);
        }
        if (ori_var_id != var_id) { // 起始varId预期通过LoadArg赋值
            item = var_id2_arg_index_map.find(ori_var_id);
            if (item == var_id2_arg_index_map.end()) {
                HCCL_ERROR("[%s]fail, Invalid goSize variable id(%u), oriVarId = %u", __func__, var_id, ori_var_id);
                return HCCL_E_PARA;
            }
        } else {
            HCCL_ERROR("[%s]fail, Invalid goSize variable id(%u)", __func__, var_id);
            return HCCL_E_PARA;
        }
    }
    HCCL_INFO("[GetArgIndex] find end");
    if (item->second >= arg_size) {
        HCCL_ERROR("Invalid goSize variable index(%u).", item->second);
        return HCCL_E_PARA;
    }
    HCCL_INFO(
        "GetArgIndex success: varId(%u) varId2VarIdMapSize(%u) varId2ArgIndexMapSize(%u) taskArgsSize(%u)", var_id,
        var_id2_var_id_map.size(), var_id2_arg_index_map.size(), arg_size);
    arg_index = task_args[item->second];
    return HCCL_SUCCESS;
}

void dump_ccu_profiling_info(const std::vector<ccu_profiling_info>& profiling_infos)
{
    auto dump_link_info = [](const ccu_profiling_info& info) -> void {
        for (int i = 0; i < ccu_max_channel_num; i++) {
            if (info.channel_id[i] == invalid_value_channelid) {
                continue;
            }
            HCCL_INFO("channelId(%u), remoteRankId(%u).", info.channel_id[i], info.remote_rank_id[i]);
        }
    };

    for (const auto& prof_info : profiling_infos) {
        if (prof_info.type == static_cast<uint8_t>(ccu_profilin_type::ccu_task_profiling)) {
            HCCL_INFO(
                "Dump CCU Profiling Info:SQE Profiling Info: ctxSignautre(%s), "
                "dieId(%d), missionId(%d), instrId(%d).",
                prof_info.name.c_str(), static_cast<int>(prof_info.die_id), static_cast<int>(prof_info.mission_id),
                static_cast<int>(prof_info.instr_id));
        } else if (prof_info.type == static_cast<uint8_t>(ccu_profilin_type::ccu_waitcke_profiling)) {
            HCCL_INFO(
                "Microcode WaitCKE Profiling Info: name(%s), "
                "dieId(%d), missionId(%d), instrId(%d), ckeId(%u), mask(%u).",
                prof_info.name.c_str(), static_cast<int>(prof_info.die_id), static_cast<int>(prof_info.mission_id),
                static_cast<int>(prof_info.instr_id), prof_info.cke_id, prof_info.mask);
            dump_link_info(prof_info);
        } else if (prof_info.type == static_cast<uint8_t>(ccu_profilin_type::ccu_loopgroup_profiling)) {
            HCCL_INFO(
                "Microcode LoopGroup Profiling Info: name(%s), "
                "dieId(%d), missionId(%d), instrId(%d), reduceOpType(%d), inputDataType(%d), "
                "outputDataType(%d), dataSize(%llu).",
                prof_info.name.c_str(), static_cast<int>(prof_info.die_id), static_cast<int>(prof_info.mission_id),
                static_cast<int>(prof_info.instr_id), static_cast<int>(prof_info.reduce_op_type),
                static_cast<int>(prof_info.input_data_type), static_cast<int>(prof_info.output_data_type),
                prof_info.data_size);
            dump_link_info(prof_info);
        }
    }
}

constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint16_t parse_repeat_num_from_parallel_param(uint64_t parallel_param)
{
    constexpr uint16_t repeat_bit_num = 7;        // 7： repeat num 占 7 bits
    constexpr uint16_t repeat_num_shift_bit = 55; // 55： repeat num占[61:55]位置
    return (parallel_param >> repeat_num_shift_bit) & set_bits(repeat_bit_num);
}

HcclResult ccu_kernel::collect_sqe_and_wait_cke_profiling_info()
{
    auto& ccu_profiling_cache = get_profiling_info();
    uint32_t count{0};
    HCCL_INFO("[GetCcuProfilingInfo] Process sqe&waitcke profiling info start.");
    for (auto& prof_info : ccu_profiling_cache) {
        prof_info.mission_id = get_mission_id();
        if (prof_info.type == static_cast<uint8_t>(asc::ccu_profilin_type::ccu_task_profiling)) {
            prof_info.instr_id = get_instr_id();
            all_ccu_profiling_infos_.push_back(prof_info);
            continue;
        }
        if (count >= get_waite_cke_profiling_reps().size()) {
            HCCL_ERROR(
                "count[%u] out of range[0, %u], cache size(%u).", count, get_waite_cke_profiling_reps().size(),
                ccu_profiling_cache.size());
            return HCCL_E_INTERNAL;
        }
        auto wait_cke_rep = get_waite_cke_profiling_reps()[count];
        prof_info.instr_id = wait_cke_rep->start_instr_id();
        if (prof_info.cke_id == invalid_cke_id) { // localWait Rep
            if (wait_cke_rep.get() == nullptr) {
                HCCL_ERROR("[GetCcuProfilingInfo] localWaitRep is nullptr.");
                return HCCL_E_PTR;
            }
            prof_info.cke_id = wait_cke_rep->get_id();
            HCCL_INFO("[CcuKernel][GetCcuProfilingInfo] waitcke[%u]", prof_info.cke_id);
        }
        all_ccu_profiling_infos_.push_back(prof_info);
        count++;
    }
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::build_loop_group_var_id_maps(
    std::unordered_map<uint16_t, uint32_t>& var_id2_arg_index_map,
    std::unordered_map<uint16_t, uint16_t>& var_id2_var_id_map)
{
    auto& lg_prof_info = get_lg_profiling_info();
    HCCL_INFO(
        "[GetCcuProfilingInfo] create varId2ArgIndexMap start. size=%lu", lg_prof_info.load_rep2_arg_idx_map.size());
    for (auto& iter : lg_prof_info.load_rep2_arg_idx_map) {
        if (iter.first.get() == nullptr) {
            HCCL_ERROR("[GetCcuProfilingInfo] loadRep is nullptr.");
            return HCCL_E_PTR;
        }
        auto load_rep = dynamic_cast<ccu_rep::ccu_rep_load_arg*>(iter.first.get());
        var_id2_arg_index_map[load_rep->get_var_id()] = iter.second;
    }

    HCCL_INFO("[GetCcuProfilingInfo] create varId2VarIdMap start. size=%lu", lg_prof_info.assign_profiling_reps.size());
    for (auto& iter : lg_prof_info.assign_profiling_reps) {
        if (iter.get() == nullptr) {
            HCCL_ERROR("[GetCcuProfilingInfo] assignRep is nullptr.");
            return HCCL_E_PTR;
        }
        auto assign_rep = dynamic_cast<ccu_rep::ccu_rep_assign*>(iter.get());
        var_id2_var_id_map[assign_rep->get_var_b().id()] = assign_rep->get_var_a().id();
    }
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::collect_loop_group_profiling_info(
    const uint64_t* task_args, uint32_t arg_size, const std::unordered_map<uint16_t, uint32_t>& var_id2_arg_index_map,
    const std::unordered_map<uint16_t, uint16_t>& var_id2_var_id_map)
{
    auto& lg_prof_info = get_lg_profiling_info();
    HCCL_INFO(
        "[GetCcuProfilingInfo] process loop group profiling start: "
        "lgsize(%lu), goSize(%lu)",
        lg_prof_info.lg_profiling_reps.size(), group_op_size_info_.size());
    const size_t safe_size = std::min(
        {lg_prof_info.lg_profiling_reps.size(), group_op_size_info_.size(), lg_prof_info.ccu_profiling_infos.size()});
    if (safe_size != lg_prof_info.lg_profiling_reps.size()) {
        HCCL_WARNING(
            "[CollectLoopGroupProfilingInfo] size mismatch: lgReps[%zu], goSize[%zu], profiles[%zu], safeSize[%zu].",
            lg_prof_info.lg_profiling_reps.size(), group_op_size_info_.size(), lg_prof_info.ccu_profiling_infos.size(),
            safe_size);
    }
    for (uint32_t i = 0; i < safe_size; i += 2) { // 2: 一个goSize对应一个ccu_profiling_info，对应1个loopGroup Rep
        if (arg_size == 0 || var_id2_arg_index_map.empty()) {
            continue;
        }
        uint64_t loop_param{0};
        CHK_RET(get_arg_index(
            var_id2_var_id_map, var_id2_arg_index_map, task_args, arg_size, group_op_size_info_[i].loop_param_id,
            loop_param));
        uint64_t parallel_param{0};
        CHK_RET(get_arg_index(
            var_id2_var_id_map, var_id2_arg_index_map, task_args, arg_size, group_op_size_info_[i].parallel_param_id,
            parallel_param));
        HCCL_INFO(
            "Collect loopgroup profiling info: repSize[%u], index[%u],"
            "loopParam[%llu], parallelParam[%llu].",
            lg_prof_info.lg_profiling_reps.size(), i, loop_param, parallel_param);

        if (loop_param != 0) {
            lg_prof_info.ccu_profiling_infos[i].data_size = loop_param * mo_config_.loop_count * mo_config_.mem_slice;
            lg_prof_info.ccu_profiling_infos[i].instr_id =
                dynamic_cast<ccu_rep::ccu_rep_loop_group_bundle*>(lg_prof_info.lg_profiling_reps[i].get())
                    ->start_instr_id();
            all_ccu_profiling_infos_.push_back(lg_prof_info.ccu_profiling_infos[i]);
        }

        if (parallel_param != 0) {
            if (i + 1 >= safe_size) {
                HCCL_WARNING(
                    "[CollectLoopGroupProfilingInfo] no paired rep for parallel branch, index[%u], safeSize[%zu].", i,
                    safe_size);
                continue;
            }
            HCCL_INFO("[GetCcuProfilingInfo] collect lg, residual start i=%lu", i);
            uint64_t residual{0};
            CHK_RET(get_arg_index(
                var_id2_var_id_map, var_id2_arg_index_map, task_args, arg_size, group_op_size_info_[i].residual_id,
                residual));
            uint64_t repeat_num = parse_repeat_num_from_parallel_param(parallel_param);
            lg_prof_info.ccu_profiling_infos[i].data_size = repeat_num * mo_config_.mem_slice + residual;
            lg_prof_info.ccu_profiling_infos[i].instr_id =
                dynamic_cast<ccu_rep::ccu_rep_loop_group_bundle*>(lg_prof_info.lg_profiling_reps[i + 1].get())
                    ->start_instr_id();
            all_ccu_profiling_infos_.push_back(lg_prof_info.ccu_profiling_infos[i]);
        }
    }
    return HCCL_SUCCESS;
}

/*
 * variable/maskSignal等资源变量Id，一定要在获取ccu profiling时才获取；
 * 原因：在创建context Rep时，其资源Id属于虚拟资源；翻译时，才会绑定固定的物理资源。
 */
HcclResult ccu_kernel::get_ccu_profiling_info(
    const uint64_t* task_args, uint32_t arg_size, std::vector<ccu_profiling_info>& all_ccu_profiling_info)
{
    HCCL_INFO("[GetCcuProfilingInfo] Enter.");
    all_ccu_profiling_infos_.clear();

    CHK_RET(collect_sqe_and_wait_cke_profiling_info());

    std::unordered_map<uint16_t, uint32_t> var_id2_arg_index_map;
    std::unordered_map<uint16_t, uint16_t> var_id2_var_id_map;
    CHK_RET(build_loop_group_var_id_maps(var_id2_arg_index_map, var_id2_var_id_map));

    CHK_RET(collect_loop_group_profiling_info(task_args, arg_size, var_id2_arg_index_map, var_id2_var_id_map));

    dump_ccu_profiling_info(all_ccu_profiling_infos_);
    all_ccu_profiling_info = all_ccu_profiling_infos_;
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::add_profiling_info(
    const ChannelHandle* channels, uint32_t channel_num, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, const std::string& op_name)
{
    CHK_PTR_NULL(channels);
    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_loopgroup_profiling);
    ccu_profiling_info_cache.name = op_name;
    ccu_profiling_info_cache.reduce_op_type = op_type;
    ccu_profiling_info_cache.input_data_type = data_type;
    ccu_profiling_info_cache.output_data_type = output_data_type;
    ccu_profiling_info_cache.mission_id = get_mission_id();

    CHK_SAFETY_FUNC_RET(memset_s(
        ccu_profiling_info_cache.channel_id, sizeof(ccu_profiling_info_cache.channel_id), invalid_value_channelid,
        sizeof(ccu_profiling_info_cache.channel_id)));
    for (uint32_t i = 0; i < channel_num; i++) {
        ccu_channel channel_impl(channels[i]);
        CHK_RET(channel_impl.get_result());
        ccu_profiling_info_cache.channel_id[i] = channel_impl->get_channel_id();
        ccu_profiling_info_cache.channel_handle[i] = channels[i];
        HCCL_INFO(
            "[%s]type[%d], name[%s], opType[%d], dataType[%d], outputDataType[%d], missionId[%u], "
            "channelHandle[0x%llx], channelId[%u]",
            __func__, ccu_profiling_info_cache.type, ccu_profiling_info_cache.name.c_str(), op_type, data_type,
            output_data_type, ccu_profiling_info_cache.mission_id, ccu_profiling_info_cache.channel_handle[i],
            ccu_profiling_info_cache.channel_id[i]);
    }
    lg_profiling_info.ccu_profiling_infos.push_back(ccu_profiling_info_cache);
    lg_profiling_info.lg_profiling_reps.push_back(all_lg_profiling_reps.back());
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::add_ccu_profiling(
    group_info group_info, const std::vector<ChannelHandle> channel_handle, HcclDataType data_type,
    HcclDataType output_data_type, HcclReduceOp op_type, const std::string& op_name)
{
    CHK_RET(
        add_ccu_profiling(channel_handle.data(), channel_handle.size(), data_type, output_data_type, op_type, op_name));
    group_op_size_info_.push_back(group_info);
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::add_ccu_profiling(
    const ChannelHandle* channels, uint32_t channel_num, HcclDataType data_type, HcclDataType output_data_type,
    HcclReduceOp op_type, const std::string& op_name)
{
    CHK_PTR_NULL(channels);
    CHK_RET(add_profiling_info(channels, channel_num, data_type, output_data_type, op_type, op_name));
    return HCCL_SUCCESS;
}

HcclResult ccu_kernel::add2_const_value2_var_map(std::vector<uint64_t>& values)
{
    // 记录当前context所需的常量，仅A6场景适用
    for (uint64_t value : values) {
        if (const_value2_var_map_.find(value) == const_value2_var_map_.end()) {
            const_value2_var_map_[value] = create_variable();
        }
    }
    return HCCL_SUCCESS;
}

}; // namespace asc
