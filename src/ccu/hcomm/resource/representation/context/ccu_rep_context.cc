/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/channel/ccu_channel.h"

namespace asc {
namespace ccu_rep {

ccu_rep_context::ccu_rep_context()
{
    main_block_ = std::make_shared<ccu_rep::ccu_rep_block>(ins_generator_);
    active_block_ = main_block_;
}

ccu_rep_context::~ccu_rep_context() {}

std::shared_ptr<ccu_rep::ccu_rep_block> ccu_rep_context::current_block()
{
    if (active_block_ == nullptr) {
        asc::throw_ccu_internal("Invalid ActiveBlock");
    }
    return active_block_;
}

void ccu_rep_context::set_current_block(std::shared_ptr<ccu_rep::ccu_rep_block> rep_block)
{
    active_block_ = rep_block;
}

/**
 * @details:保存rep信息，后续有arg后配合补全profiling信息
 */
void ccu_rep_context::collect_profiling_reps(std::shared_ptr<ccu_rep::ccu_rep_base> rep)
{
    if (rep->type() == ccu_rep_type::assign) {
        auto assign_rep = dynamic_cast<ccu_rep_assign*>(rep.get());
        if (assign_rep->get_sub_type() == assign_sub_type::var_to_var) {
            lg_profiling_info.assign_profiling_reps.push_back(rep);
        }
    } else if (
        current_block()->type() != ccu_rep::ccu_rep_type::loop_block &&
        (rep->type() == ccu_rep_type::loc_wait_event || rep->type() == ccu_rep_type::rem_wait_sem ||
         rep->type() == ccu_rep_type::rem_wait_group)) {
        wait_cke_profiling_reps.push_back(rep);
    } else if (rep->type() == ccu_rep_type::loop_group) {
        all_lg_profiling_reps.push_back(rep);
    }
}

void ccu_rep_context::append(std::shared_ptr<ccu_rep::ccu_rep_base> rep)
{
    collect_profiling_reps(rep);
    current_block()->append(rep);
}

const std::vector<std::shared_ptr<ccu_rep::ccu_rep_base>>& ccu_rep_context::get_rep_sequence()
{
    return main_block_->get_reps();
}

// 按指令编号在任务的主 REP 列表里找它属于哪条 REP：每条 REP 占的指令条数
// 不一样，编号落在某条的开始到结束范围内就是它；找不到返回 nullptr。
// 找到 FUNC_BLOCK 这类壳 REP 时，调用方还要进壳里继续找
std::shared_ptr<ccu_rep::ccu_rep_base> ccu_rep_context::get_rep_by_instr_id(uint16_t instr_id)
{
    for (const auto& rep : get_rep_sequence()) {
        CHK_PRT_RET(rep == nullptr, HCCL_ERROR("[%s]fail, rep is nullptr", __func__), nullptr);
        const uint16_t rep_instr_count = rep->instr_count();
        if (rep_instr_count == 0) {
            continue;
        }
        const uint16_t start_id = rep->start_instr_id();
        const uint16_t end_id = start_id + rep_instr_count - 1;
        HCCL_INFO("[%s]startId[%u], endId[%u], instrId[%u]", __func__, start_id, end_id, instr_id);
        // 编号落在这条 REP 的指令范围内，就是它
        if (instr_id >= start_id && instr_id <= end_id) {
            return rep;
        }
    }
    return nullptr;
}

void ccu_rep_context::dump_represtation()
{
    HCCL_INFO("Rep Count: %lu", get_rep_sequence().size());
    for (uint32_t index = 0; index < get_rep_sequence().size(); index++) {
        HCCL_INFO("index[%u]: %s", index, get_rep_sequence()[index]->describe().c_str());
    }
}

void ccu_rep_context::set_die_id(uint32_t die_id)
{
    HCCL_INFO("set dieId[%u]", die_id);
    this->die_id_ = die_id;
}

uint32_t ccu_rep_context::get_die_id() const { return die_id_; }

void ccu_rep_context::set_mission_id(uint32_t mission_id)
{
    if (this->mission_id_ == UINT32_MAX) {
        this->mission_id_ = mission_id;
    }
}

uint32_t ccu_rep_context::get_mission_id() const { return mission_id_; }

void ccu_rep_context::set_mission_key(uint32_t mission_key) { this->mission_key_ = mission_key; }

uint32_t ccu_rep_context::get_mission_key() const { return mission_key_; }

std::vector<ccu_profiling_info>& ccu_rep_context::get_profiling_info() { return profiling_info; }

const std::vector<std::shared_ptr<ccu_rep_base>>& ccu_rep_context::get_waite_cke_profiling_reps() const
{
    return wait_cke_profiling_reps;
}

loop_group_profiling_info& ccu_rep_context::get_lg_profiling_info() { return lg_profiling_info; }

void ccu_rep_context::add_sqe_profiling(const std::string& kernel_name)
{
    constexpr uint32_t default_die_id = 0; // 首次填写profiling时dieId未确定
    // 生成SQE粒度profiling信息
    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_task_profiling);
    ccu_profiling_info_cache.name = kernel_name.c_str();
    ccu_profiling_info_cache.die_id = default_die_id;
    HCCL_DEBUG(
        "[%s]type[%d], name[%s], deafultDieId[0]", __func__, ccu_profiling_info_cache.type,
        ccu_profiling_info_cache.name.c_str(), ccu_profiling_info_cache.die_id);
    profiling_info.push_back(ccu_profiling_info_cache);
}

int32_t ccu_rep_context::add_profiling(const std::string& name, uint32_t mask)
{
    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_waitcke_profiling);
    ccu_profiling_info_cache.name = name;
    ccu_profiling_info_cache.cke_id = invalid_cke_id;
    ccu_profiling_info_cache.mask = mask;
    CHK_SAFETY_FUNC_RET(memset_s(
        ccu_profiling_info_cache.channel_id, sizeof(ccu_profiling_info_cache.channel_id), invalid_value_channelid,
        sizeof(ccu_profiling_info_cache.channel_id)));

    HCCL_INFO("[%s]name[%s], mask[%u], type[%d]", __func__, name.c_str(), mask, ccu_profiling_info_cache.type);
    profiling_info.push_back(ccu_profiling_info_cache);
    return HCCL_SUCCESS;
}

int32_t ccu_rep_context::add_profiling(
    const ChannelHandle channel, const std::string& name, uint32_t signal_index, uint32_t mask)
{
    ccu_channel channel_impl(channel);
    CHK_RET(channel_impl.get_result());

    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_waitcke_profiling);
    ccu_profiling_info_cache.name = name;
    CHK_RET(channel_impl->get_loc_cke_by_index(signal_index, ccu_profiling_info_cache.cke_id));
    ccu_profiling_info_cache.mask = mask;
    CHK_SAFETY_FUNC_RET(memset_s(
        ccu_profiling_info_cache.channel_id, sizeof(ccu_profiling_info_cache.channel_id), invalid_value_channelid,
        sizeof(ccu_profiling_info_cache.channel_id)));
    ccu_profiling_info_cache.channel_id[0] = channel_impl->get_channel_id();
    ccu_profiling_info_cache.channel_handle[0] = channel;

    HCCL_INFO(
        "[%s]channelHandle[0x%llx], name[%s], signalIndex[%u], mask[%u], type[%d], ckeId[%u], channelId[%u]", __func__,
        channel, name.c_str(), signal_index, mask, ccu_profiling_info_cache.type, ccu_profiling_info_cache.cke_id,
        ccu_profiling_info_cache.channel_id[0]);
    profiling_info.push_back(ccu_profiling_info_cache);
    return HCCL_SUCCESS;
}

int32_t ccu_rep_context::add_profiling(const ChannelHandle* channels, uint32_t channel_num)
{
    CHK_PTR_NULL(channels);
    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_loopgroup_profiling);
    ccu_profiling_info_cache.name = "GroupBroadcast";
    ccu_profiling_info_cache.reduce_op_type = 0xFF;   // 0xFF 无效值
    ccu_profiling_info_cache.input_data_type = 0xFF;  // 0xFF 无效值
    ccu_profiling_info_cache.output_data_type = 0xFF; // 0xFF 无效值
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
            "[%s]type[%d], name[%s], missionId[%u], channelHandle[0x%llx], channelId[%u]", __func__,
            ccu_profiling_info_cache.type, ccu_profiling_info_cache.name.c_str(), ccu_profiling_info_cache.mission_id,
            ccu_profiling_info_cache.channel_handle[i], ccu_profiling_info_cache.channel_id[i]);
    }

    lg_profiling_info.ccu_profiling_infos.push_back(ccu_profiling_info_cache);
    if (!all_lg_profiling_reps.empty()) {
        lg_profiling_info.lg_profiling_reps.push_back(all_lg_profiling_reps.back());
    }
    return HCCL_SUCCESS;
}

int32_t ccu_rep_context::add_profiling(
    const ChannelHandle* channels, uint32_t channel_num, int32_t hcomm_data_type, int32_t hcomm_output_data_type,
    int32_t hcomm_op_type)
{
    HcclDataType data_type = static_cast<HcclDataType>(hcomm_data_type);
    HcclDataType output_data_type = static_cast<HcclDataType>(hcomm_output_data_type);
    HcclReduceOp op_type = static_cast<HcclReduceOp>(hcomm_op_type);

    CHK_PTR_NULL(channels);
    ccu_profiling_info_cache.type = static_cast<uint8_t>(ccu_profilin_type::ccu_loopgroup_profiling);
    ccu_profiling_info_cache.name = "GroupReduce";
    ccu_profiling_info_cache.reduce_op_type = op_type;
    ccu_profiling_info_cache.input_data_type = data_type;
    ccu_profiling_info_cache.output_data_type = output_data_type;
    ccu_profiling_info_cache.mission_id = get_mission_id();

    CHK_SAFETY_FUNC_RET(memset_s(
        ccu_profiling_info_cache.channel_id, sizeof(ccu_profiling_info_cache.channel_id), invalid_value_channelid,
        sizeof(ccu_profiling_info_cache.channel_id)));
    for (uint32_t i = 0; i < channel_num; ++i) {
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

void ccu_rep_context::set_dependency_info(uint32_t id, uint32_t mask, const std::shared_ptr<ccu_rep_base>& rep)
{
    // 按 mask 各置位 bit 分别登记：异常侧按 1<<i 单 bit 查询，多 bit mask 需拆解到每个单 bit key
    constexpr uint32_t ccu_cke_bit_num = 16; // CKE 的 bit 数最多为 16
    auto& inner = dep_info_[id];
    for (uint32_t i = 0; i < ccu_cke_bit_num; i++) {
        uint32_t bit = 1u << i;
        if ((mask & bit) != 0u) {
            inner[bit].push_back(rep);
        }
    }
}

std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>> ccu_rep_context::get_dependency_info(
    uint32_t id)
{
    // 查找给定 id 是否存在于 depInfo 中
    auto it = dep_info_.find(id);
    // 如果找到 id，返回与之关联的内层 unordered_map
    if (it != dep_info_.end()) {
        return it->second;
    }
    // 如果未找到 id，返回一个空的 unordered_map
    return std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>>();
}

void ccu_rep_context::erase_dependency_info(uint32_t id) { dep_info_.erase(id); }

void ccu_rep_context::clear_dependency_info() { dep_info_.clear(); }

}; // namespace ccu_rep
}; // namespace asc
