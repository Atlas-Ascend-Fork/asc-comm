/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/translator/ccu_rep_translator_v1.h"

#include <algorithm>

#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopcall_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funccall_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loop_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_loadarg_v1.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "hcomm/resource/microcode_opt/microcode_optimizer.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

namespace asc {
namespace ccu_rep {

uint32_t ccu_rep_translator::ccu_version = HCOMM_CCU_VERSION_INVALID;

template <typename t>
bool check_type(const std::shared_ptr<ccu_rep_block>& refer)
{
    if (refer == nullptr) {
        HCCL_ERROR("[%s] input refer is nullptr", __func__);
        return false;
    }
    HCCL_INFO("[check_type] refer->Type() = %d", refer->type());
    return false;
}

template <>
bool check_type<ccu_rep_func_block>(const std::shared_ptr<ccu_rep_block>& refer)
{
    if (refer == nullptr) {
        HCCL_ERROR("[%s] input refer is nullptr", __func__);
        return false;
    }
    return refer->type() == ccu_rep_type::func_block ? true : false;
}

template <>
bool check_type<ccu_rep_loop_block>(const std::shared_ptr<ccu_rep_block>& refer)
{
    if (refer == nullptr) {
        HCCL_ERROR("[%s] input refer is nullptr", __func__);
        return false;
    }
    return refer->type() == ccu_rep_type::loop_block ? true : false;
}

template <typename t1, typename t2>
void ccu_rep_translator::build_reference(const std::shared_ptr<ccu_rep_base>& rep)
{
    auto caller = std::static_pointer_cast<t1>(rep);
    auto label = caller->get_label();
    // 特例：针对函数地址调用，不需要依靠函数名索引
    if (label == "") {
        return;
    }
    auto refer = ref_manager_->get_ref_block(label);
    if (check_type<t2>(refer)) {
        caller->reference(std::static_pointer_cast<t2>(refer));
    } else {
        asc::throw_ccu_internal("Invalid reference: %s", label.c_str());
    }
}

ccu_rep_translator::ccu_rep_translator(
    std::shared_ptr<ccu_rep_reference_manager> ref_manager, const trans_dep& trans_dep)
    : ref_manager_(ref_manager), trans_dep_(trans_dep)
{
    if (ccu_version == HCOMM_CCU_VERSION_INVALID) {
        asc::throw_ccu_internal("[CcuRepTranslator] Constructor: Invalid CCU Type!");
    }
}

void ccu_rep_translator::set_ccu_version(uint32_t version) { ccu_version = version; }

uint32_t ccu_rep_translator::get_instr_num()
{
    return ccu_version == HCOMM_CCU_VERSION_V1 ?
               4 // 4:翻译器翻译过程中额外需要的指令空间大小(插入3条通用操作指令+1条终止指令)
               :
               13; // 13:翻译器翻译过程中额外需要的指令空间大小(插入3条通用操作指令+1条终止指令+9条repJump)
}

void ccu_rep_translator::get_res(ccu_rep_resource& res)
{
    int var_num = xn_num;
    int gsa_num = ccu_version == HCOMM_CCU_VERSION_V1 ? ccu_translator_gsa_num : 0;
    for (int i = 0; i < var_num; i++) {
        res.variable[trans_dep_.die_id].push_back(var_[i]);
    }
    for (int i = 0; i < gsa_num; i++) {
        res.address[trans_dep_.die_id].push_back(addr_[i]);
    }
    for (int i = 0; i < cke_num; i++) {
        res.completed_event[trans_dep_.die_id].push_back(signal_[i]);
    }
}

void ccu_rep_translator::pre_process(std::shared_ptr<ccu_rep_base> rep)
{
    auto rep_type = rep->type();
    if (rep_type == ccu_rep_type::func_block) {
        auto func_block = std::static_pointer_cast<ccu_rep_func_block>(rep);
        ref_manager_->set_ref_block(func_block->get_label(), func_block);
        func_block->set_func_manager(ref_manager_.get());
    } else if (rep_type == ccu_rep_type::loop_block) {
        auto loop_block = std::static_pointer_cast<ccu_rep_loop_block>(rep);
        ref_manager_->set_ref_block(loop_block->get_label(), loop_block);
    } else if (rep_type == ccu_rep_type::func_call) {
        build_reference<ccu_rep_func_call, ccu_rep_func_block>(rep);
        auto func_call = std::static_pointer_cast<ccu_rep_func_call>(rep);
        func_call->set_func_manager(ref_manager_.get());
    } else if (rep_type == ccu_rep_type::loop_call) {
        build_reference<ccu_rep_loop_call, ccu_rep_loop_block>(rep);
    } else if (rep_type == ccu_rep_type::loop) {
        build_reference<ccu_rep_loop, ccu_rep_loop_block>(rep);
    }
}

void ccu_rep_translator::translate(
    ccu_kernel* ccu_kernel, const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, ccu_instr*& instr,
    uint16_t& instr_id, std::function<bool(std::shared_ptr<ccu_rep_base>)> filter)
{
    constexpr uint32_t max_try_count = 10; // 最大尝试次数10
    uint32_t try_count = 0;
    uint32_t rest_count = 0;

    auto func_in_var = ref_manager_.get()->get_func_in();
    int func_arg_index = 0;

    do {
        rest_count = 0;
        for (uint32_t index = 0; index < rep_vec.size(); index++) {
            if (!filter(rep_vec[index])) {
                continue;
            }

            if (rep_vec[index]->translated()) {
                continue;
            }

            if (rep_vec[index]->type() == ccu_rep_type::load_arg && trans_dep_.is_func_block) {
                trans_dep_.load_xn_id = func_in_var[func_arg_index++].id();
            }

            pre_process(rep_vec[index]);
            bool flag = rep_vec[index]->translate(ccu_kernel, instr, instr_id, trans_dep_);
            if (!flag) {
                rest_count++;
            }
        }
        try_count++;
        HCCL_INFO("tryCount = %u, remaining representation = %u", try_count, rest_count);
    } while (rest_count > 0 && try_count < max_try_count);

    if (try_count == max_try_count && rest_count > 0) {
        HCCL_ERROR(
            "After translation, remaining representation: tryCount = %u, restCount = %u ", try_count, rest_count);
        for (uint32_t index = 0; index < rep_vec.size(); index++) {
            if (!rep_vec[index]->translated()) {
                HCCL_ERROR("index[%u], %s", index, rep_vec[index]->describe().c_str());
            }
        }
        asc::throw_ccu_internal("Translation Failed");
    }
}

ccu_instr_info ccu_rep_translator::translate(
    ccu_kernel* ccu_kernel, const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, uint16_t start_instr_id,
    bool is_func_block)
{
    constexpr uint32_t default_instr_capacity = 32 * 1024; // 默认最大容量32 * 1024条
    ccu_instr_info instr_info;
    instr_info.instr_vec.resize(default_instr_capacity);
    ccu_instr* instr = instr_info.instr_vec.data();
    uint16_t cur_instr_id = start_instr_id;

    bind_resource(is_func_block);

    // 翻译LoopBlock
    translate(ccu_kernel, rep_vec, instr, cur_instr_id, [](std::shared_ptr<ccu_rep_base> rep) -> bool {
        return rep->type() == ccu_rep_type::loop_block;
    });

    // 翻译funcBlock
    translate(ccu_kernel, rep_vec, instr, cur_instr_id, [](std::shared_ptr<ccu_rep_base> rep) -> bool {
        return rep->type() == ccu_rep_type::func_block;
    });

    uint16_t mission_start_instr_id = cur_instr_id;

    // 翻译Load:按全局 argId 升序排序后再翻译,确保 LoadSqeArgs 指令在 mission 切分时
    // 落入与其 slot id 匹配的 mission(避免用户乱序 LoadArg 导致取参错位)
    std::vector<std::shared_ptr<ccu_rep_base>> sorted_load_arg_reps;
    sorted_load_arg_reps.reserve(rep_vec.size());
    for (const auto& rep : rep_vec) {
        if (rep->type() == ccu_rep_type::load_arg) {
            sorted_load_arg_reps.push_back(rep);
        }
    }
    std::stable_sort(
        sorted_load_arg_reps.begin(), sorted_load_arg_reps.end(),
        [](const std::shared_ptr<ccu_rep_base>& a, const std::shared_ptr<ccu_rep_base>& b) {
            return std::static_pointer_cast<ccu_rep_load_arg>(a)->get_full_arg_id() <
                   std::static_pointer_cast<ccu_rep_load_arg>(b)->get_full_arg_id();
        });
    translate(ccu_kernel, sorted_load_arg_reps, instr, cur_instr_id, [](std::shared_ptr<ccu_rep_base> rep) -> bool {
        return rep->type() == ccu_rep_type::load_arg;
    });

    // 插入通用操作
    common_process(ccu_kernel, instr, cur_instr_id);

    // 翻译主体
    translate(ccu_kernel, rep_vec, instr, cur_instr_id, [](std::shared_ptr<ccu_rep_base> rep) -> bool { return true; });

    finish_main_block(instr, cur_instr_id);

    instr_info.start_instr_id = start_instr_id;
    instr_info.instr_count = cur_instr_id - start_instr_id;
    instr_info.mission_start_instr_id = mission_start_instr_id;
    instr_info.mission_instr_count = cur_instr_id - mission_start_instr_id;
    instr_info.instr_vec.resize(instr_info.instr_count);

    dump_rep(rep_vec, instr_info);
    dump_instruction(instr_info);

    if (ccu_version == HCOMM_CCU_VERSION_V2) {
        auto optimized =
            ccu_opt::microcode_optimizer::run(instr_info, trans_dep_.reserve_xn_id, trans_dep_.reserve_cke_id);
        dump_instruction(optimized);
        return optimized;
    }

    return instr_info;
}

void ccu_rep_translator::common_process(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id)
{
    load_imd_to_xn_instr(instr++, var_[0].id(), 0);
    load_imd_to_gsa_instr(instr++, addr_[0].id(), 0);
    set_cke_instr(instr++, signal_[0].id(), 0xffff, 0, 0, 1);

    // 遍历需要赋值的常量，A5场景下暂为空表
    std::unordered_map<uint64_t, ccu_rep::variable>& const_value2_var_map = ccu_kernel->get_const_value2_var_map();
    uint32_t const_value_num = const_value2_var_map.size();
    for (auto elem : const_value2_var_map) {
        uint64_t const_value = elem.first;
        ccu_rep::variable cur_variable = elem.second;
        load_imd_to_xn_instr(instr++, cur_variable.id(), const_value);
    }

    uint32_t instr_num = 3 + const_value_num;
    if (instr_id > UINT16_MAX - instr_num) {
        asc::throw_ccu_internal("integer overflow occurs");
    }
    instr_id += instr_num; // 插入3条指令
}

void ccu_rep_translator::finish_main_block(ccu_instr*& instr, uint16_t& instr_id)
{
    if (trans_dep_.is_func_block) {
        jump_instr(instr++, ref_manager_.get()->get_func_ret(func_nest_max).id(), trans_dep_.reserve_xn_id, 1);
    } else {
        load_imd_to_xn_instr(instr++, var_[0].id(), 0);
    }
    instr_id++;

    if (instr_id > UINT16_MAX - 1) {
        asc::throw_ccu_internal("integer overflow occurs");
    }
}

void ccu_rep_translator::dump_instruction(const ccu_instr_info& instr_info) const
{
    HCCL_INFO(
        "CcuInstrInfo: startInstrId = %u, instrCount = %u, missionStartInstrId = %u, missionInstrCount = %u",
        instr_info.start_instr_id, instr_info.instr_count, instr_info.mission_start_instr_id,
        instr_info.mission_instr_count);
    for (uint16_t index = 0; index < instr_info.instr_vec.size(); index++) {
        HCCL_INFO(
            "%d: %s", instr_info.start_instr_id + index, parse_instr(instr_info.instr_vec.data() + index).c_str());
    }
}

void ccu_rep_translator::dump_rep(
    const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, const ccu_instr_info& instr_info) const
{
    HCCL_INFO("Translated Ccu Rep:");
    for (uint32_t index = 0; index < rep_vec.size(); index++) {
        uint16_t start_instr_id = rep_vec[index]->start_instr_id();
        uint32_t sum = static_cast<uint32_t>(start_instr_id) + rep_vec[index]->instr_count();
        if (sum > UINT16_MAX) {
            HCCL_ERROR(
                "instrId overflow: startInstrId[%u] + instr_count[%u] = %u exceeds UINT16_MAX", start_instr_id,
                rep_vec[index]->instr_count(), sum);
            continue;
        }
        uint16_t end_instr_id = static_cast<uint16_t>(sum);
        HCCL_INFO("rep[%u]: %s Instr[%u--%u]", index, rep_vec[index]->describe().c_str(), start_instr_id, end_instr_id);
        for (uint16_t instr_id = start_instr_id; instr_id < end_instr_id; instr_id++) {
            if (instr_id < instr_info.start_instr_id) {
                HCCL_ERROR("instrId[%u] less than startInstrId[%u]", instr_id, instr_info.start_instr_id);
                continue;
            }
            HCCL_INFO(
                "microcode[%u]: %s", instr_id,
                parse_instr(instr_info.instr_vec.data() + (instr_id - instr_info.start_instr_id)).c_str());
        }
    }
}

void ccu_rep_translator::bind_resource(bool is_func_block)
{
    trans_dep_.reserve_xn_id = var_[0].id();
    trans_dep_.reserve_gsa_id = addr_[0].id();
    trans_dep_.reserve_cke_id = signal_[0].id();
    for (int i = 0; i < xn_num - 1; i++) {
        trans_dep_.comm_xn[i] = var_[i + 1].id();
    }
    for (int i = 0; i < gsa_num - 1; i++) {
        trans_dep_.comm_gsa[i] = addr_[i + 1].id();
    }
    trans_dep_.comm_signal = signal_[1].id();
    trans_dep_.is_func_block = is_func_block;
    HCCL_INFO(
        "TransDep info: logicalId = %d, dieId = %u, reserveXnId = %u, reserveGsaId = %u, reserveCkeId = %u, "
        "innerDieChannelId = %u, interDieChannelId = %u",
        trans_dep_.logical_id, trans_dep_.die_id, trans_dep_.reserve_xn_id, trans_dep_.reserve_gsa_id,
        trans_dep_.reserve_cke_id, trans_dep_.reserve_channal_id[0], trans_dep_.reserve_channal_id[1]);
}
}; // namespace ccu_rep
}; // namespace asc
