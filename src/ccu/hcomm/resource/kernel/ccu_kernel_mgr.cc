/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/kernel/ccu_kernel_res_allocator.h"

#include <cstddef>
#include <limits>

#include "acl/acl_rt.h"
#include "hcomm/common/ccu_device_context.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "hcomm/common/ccu_dev_mem.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"

#include "hcomm/hcomm_ccu_control.h"

#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/common/ccu_kernel_func.h"

namespace asc {

namespace {

CcuResult set_request_count(uint32_t& target, size_t value)
{
    if (value > std::numeric_limits<uint32_t>::max()) {
        return CcuResult::CCU_E_INTERNAL;
    }
    target = static_cast<uint32_t>(value);
    return CcuResult::CCU_SUCCESS;
}

CcuResult build_translator_resource_request(uint32_t die_id, uint32_t ccu_version, asc_ccu_res_request& request)
{
    request = {};
    if (die_id >= HCOMM_CCU_MAX_DIE_NUM) {
        return CcuResult::CCU_E_PARA;
    }

    constexpr size_t slot_num = ccu_rep::ccu_translator_mission_slot_num;
    constexpr size_t xn_per_slot = ccu_rep::ccu_reference_manager_xn_num + ccu_rep::ccu_translator_xn_num;
    constexpr size_t cke_per_slot = ccu_rep::ccu_translator_cke_num;
    constexpr size_t gsa_per_slot = ccu_rep::ccu_translator_gsa_num;

    CCU_CHK_RET(set_request_count(request.count[HCOMM_CCU_BATCH_RES_XN][die_id], slot_num * xn_per_slot));
    CCU_CHK_RET(set_request_count(request.count[HCOMM_CCU_BATCH_RES_CKE][die_id], slot_num * cke_per_slot));
    if (ccu_version == HCOMM_CCU_VERSION_V1) {
        CCU_CHK_RET(set_request_count(request.count[HCOMM_CCU_BATCH_RES_GSA][die_id], slot_num * gsa_per_slot));
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult clear_kernel_instruction_resource(ccu_kernel& kernel)
{
    HcommCcuResRangePod range{};
    if (!kernel.get_instruction_resource(range)) {
        return CcuResult::CCU_SUCCESS;
    }
    kernel.clear_instruction_resource();
    return CcuResult::CCU_SUCCESS;
}

} // namespace

ccu_kernel_mgr::~ccu_kernel_mgr()
{
    if (!initialized_flag_) {
        return;
    }

    if (instruction_load_dev_mem_) {
        HCCL_RUN_INFO(
            "[CcuKernelMgr][~CcuKernelMgr]: deviceLogicId[%d], free addr[%p]", dev_logic_id_,
            instruction_load_dev_mem_);
        (void)aclrtFree(instruction_load_dev_mem_);
        instruction_load_dev_mem_ = nullptr;
        instruction_load_dev_mem_size_ = 0;
    }

    (void)deinit();
}

ccu_kernel_mgr& ccu_kernel_mgr::get_instance(const int32_t device_logic_id)
{
    static ccu_kernel_mgr kernel_manager[ccu_max_device_num + 1];

    int32_t dev_logic_id = device_logic_id;
    if (dev_logic_id < 0 || static_cast<uint32_t>(dev_logic_id) >= ccu_max_device_num) {
        HCCL_WARNING(
            "[CcuKernelMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.",
            __func__, dev_logic_id, ccu_max_device_num);
        dev_logic_id = ccu_max_device_num; // 使用备份设备
    }

    kernel_manager[dev_logic_id].dev_logic_id_ = dev_logic_id;
    return kernel_manager[dev_logic_id];
}

HcclResult ccu_kernel_mgr::init()
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    initialized_flag_ = true;
    kernel_map_.clear();
    return HcclResult::HCCL_SUCCESS;
}

CcuResult ccu_kernel_mgr::configure(asc_ccu_res_snapshot& res_pack)
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    dev_logic_id_ = res_pack.get_device_logic_id();
    translators_.clear();
    reference_mgrs_.clear();
    ccu_version_ = res_pack.get_ccu_version();
    control_ops_ = res_pack.get_control_ops();
    ccu_rep::ccu_rep_translator::set_ccu_version(ccu_version_);
    for (uint32_t die_id = 0; die_id < HCOMM_CCU_MAX_DIE_NUM; ++die_id) {
        if ((res_pack.get_valid_die_mask() & (1U << die_id)) == 0) {
            continue;
        }
        const HcommCcuDieMetadataPod* metadata = res_pack.get_die_metadata(die_id);
        CCU_CHK_PTR_NULL(metadata);
        mission_keys_[die_id] = metadata->missionKey;
        HcclResult trans_ret = instantiation_translator(static_cast<uint16_t>(die_id), res_pack);
        if (trans_ret != HcclResult::HCCL_SUCCESS) {
            translators_.clear();
            reference_mgrs_.clear();
            return static_cast<CcuResult>(trans_ret);
        }
    }
    ins_gene_ptr_ = std::make_shared<ccu_rep::ccu_ins_generater_v1>();
    initialized_flag_ = true;
    return CcuResult::CCU_SUCCESS;
}

const HcommCcuControlOpsPod& ccu_kernel_mgr::get_control_ops() const { return control_ops_; }

HcclResult ccu_kernel_mgr::deinit()
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    HcclResult first_error = HcclResult::HCCL_SUCCESS;
    for (auto& item : kernel_map_) {
        if (item.second == nullptr) {
            continue;
        }
        const CcuResult ret = clear_kernel_instruction_resource(*item.second);
        if (first_error == HcclResult::HCCL_SUCCESS && ret != CcuResult::CCU_SUCCESS) {
            first_error = static_cast<HcclResult>(ret);
        }
    }
    initialized_flag_ = false;
    kernel_map_.clear();
    translators_.clear();
    reference_mgrs_.clear();
    return first_error;
}

CcuResult ccu_kernel_mgr::Register(
    asc_ccu_res_snapshot& res_pack, const uint32_t die_id, const char* kernel_func_name, const void* kernel_func,
    const void** kernel_args, const uint32_t arg_num, ccu_kernel_handle& kernel_handle)
{
    // 允许kernelFuncName未空，此时传递默认名称
    (void)kernel_func_name;
    CCU_CHK_PTR_NULL(kernel_func);

    // 当前argNum仅允许 0 或 1
    if (arg_num > 1) {
        HCCL_ERROR("[%s] failed, argNum[%u] now only support 0 or 1.", __func__, arg_num);
        return CcuResult::CCU_E_PARA;
    }

    // 注意处理时序，需要先重置后处理rep
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    curr_kernel_ = std::make_unique<ccu_kernel>(); // 重置待注册kernel
    curr_kernel_->set_die_id(0);                   // 资源先记在 die 0，确定实际 die 后再迁移
    CCU_CHK_RET(curr_kernel_->setup_profiling_info(kernel_func_name));

    // 初始化翻译器（需在执行kernel func前设置，因为func执行时会创建rep对象）
    curr_kernel_->set_ins_generater(ins_gene_ptr_.get());
    curr_kernel_->set_ccu_version(ccu_version_);

    if (arg_num == 0) {
        auto ccu_kernel_func = reinterpret_cast<ccu_kernel_func_no_arg>(kernel_func);
        CCU_CHK_RET(ccu_kernel_func()); // 执行算法流程，生成rep和计算资源占用
    } else {
        CCU_CHK_PTR_NULL(kernel_args);
        const void* kernel_arg = kernel_args[0];
        CCU_CHK_PTR_NULL(kernel_arg);
        const auto kernel_arg_value = const_cast<ccu_kernel_arg>(kernel_arg);
        auto ccu_kernel_func = reinterpret_cast<ccu_kernel_func_one_arg>(kernel_func);
        CCU_CHK_RET(ccu_kernel_func(kernel_arg_value)); // 执行算法流程，生成rep和计算资源占用
    }

    curr_kernel_->flush_closable_pending_ifs(); // 处理未闭合的if
    CCU_CHK_RET(curr_kernel_->validate_and_apply_die(die_id, res_pack.get_valid_die_mask()));
    CCU_CHK_RET(prepare_const_value_resources()); // 记录翻译过程所需常量并申请对应资源

    CcuResult ret = alloc_res(res_pack);
    if (ret != CcuResult::CCU_SUCCESS) {
        HCCL_WARNING("[%s] AllocRes failed, maybe resource not enough, please check ret[%d]", __func__, ret);
        return ret;
    }

    kernel_id_++;
    kernel_map_[kernel_id_] = std::move(curr_kernel_);

    kernel_handle = kernel_id_;
    return CcuResult::CCU_SUCCESS;
}

static void dump_res_req_info(const asc_ccu_res_request& total_res)
{
    for (uint32_t i = 0; i < ccu_max_iodie_num; i++) {
        if (total_res.count[HCOMM_CCU_BATCH_RES_MS][i] != 0 || total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_CKE][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_LOOP][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_GSA][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i] != 0 || total_res.count[HCOMM_CCU_BATCH_RES_XN][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i] != 0 ||
            total_res.count[HCOMM_CCU_BATCH_RES_MISSION][i] != 0 || total_res.instruction[i] != 0) {
            HCCL_INFO(
                "DumpResReqInfo: dieId[%u], msReq[%u], blockMsReq[%u], ckeReq[%u], blockCkeReq[%u], "
                "loopEngineReq[%u], blockLoopEngineReq[%u], gsaReq[%u], blockGsaReq[%u], xnReq[%u], "
                "blockXnReq[%u], missionReq[%u], instructionReq[%u]",
                i, total_res.count[HCOMM_CCU_BATCH_RES_MS][i], total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][i],
                total_res.count[HCOMM_CCU_BATCH_RES_CKE][i], total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][i],
                total_res.count[HCOMM_CCU_BATCH_RES_LOOP][i], total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][i],
                total_res.count[HCOMM_CCU_BATCH_RES_GSA][i], total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][i],
                total_res.count[HCOMM_CCU_BATCH_RES_XN][i], total_res.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][i],
                total_res.count[HCOMM_CCU_BATCH_RES_MISSION][i], total_res.instruction[i]);
        }
    }
}

static size_t compute_kernel_instr_region_size(ccu_kernel& kernel)
{
    return static_cast<size_t>(kernel.get_instr_count()) + ccu_rep::ccu_rep_translator::get_instr_num() +
           kernel.get_const_value2_var_map().size() +
           static_cast<size_t>(kernel.get_rep_need_to_add_latency()) * ccu_rep::ccu_cke_raw_latency;
}

static CcuResult set_instr_request(std::unique_ptr<ccu_kernel>& kernel, asc_ccu_res_request& request)
{
    const size_t instr_count = compute_kernel_instr_region_size(*kernel);
    const uint32_t die_id = kernel->get_die_id();
    return set_request_count(request.instruction[die_id], instr_count);
}

static CcuResult set_kernel_instruction_resource(
    std::unique_ptr<ccu_kernel>& kernel, const asc_ccu_res_repository& allocated)
{
    const uint32_t die_id = kernel->get_die_id();
    const auto& instruction_ranges = allocated.instruction[die_id];
    if (instruction_ranges.empty()) {
        HCCL_ERROR("[CcuKernelMgr][%s] failed, dieId[%u] does not have instruction resource.", __func__, die_id);
        return CcuResult::CCU_E_UNAVAIL;
    }
    const HcommCcuResRangePod& ins_info = instruction_ranges.front();
    HCCL_INFO(
        "[CcuKernelMgr][%s]: dieId[%u], startId[%u], count[%u]", __func__, die_id, ins_info.startId, ins_info.count);
    kernel->set_instr_id(ins_info.startId);
    kernel->set_instruction_resource(ins_info);

    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_mgr::prepare_const_value_resources()
{
    // insGenerator统计rep中常量，并填写当前kernel的常量表，当前只有A6有对应处理，A5没有常量处理需求
    CCU_CHK_PTR_NULL(curr_kernel_);
    const auto& rep_vec = curr_kernel_->get_rep_sequence();

    const auto& translator = translators_[curr_kernel_->get_die_id()][0];
    CCU_CHK_PTR_NULL(translator);
    const auto& trans_dep = translator->get_trans_dep(); // 此时未分配missionid，取0对应的transDep读取常量
    CCU_CHK_PTR_NULL(ins_gene_ptr_);
    for (uint32_t index = 0; index < rep_vec.size(); index++) {
        const auto& cur_rep_type = rep_vec[index]->type();
        ccu_rep::ccu_rep_base* cur_rep_ptr = rep_vec[index].get();
        CCU_CHK_PTR_NULL(cur_rep_ptr);

        // 遍历每个rep，包括repBlock中的每个rep，将常量资源需求记录在currkernel中
        ins_gene_ptr_->prepare_const_value(cur_rep_ptr, trans_dep, curr_kernel_.get());
        if (cur_rep_type == ccu_rep::ccu_rep_type::block || cur_rep_type == ccu_rep::ccu_rep_type::func_block ||
            cur_rep_type == ccu_rep::ccu_rep_type::loop_block) {
            ccu_rep::ccu_rep_block* cur_rep_block_ptr = static_cast<ccu_rep::ccu_rep_block*>(cur_rep_ptr);
            CCU_CHK_PTR_NULL(cur_rep_block_ptr);
            for (const auto& rep_in_block : cur_rep_block_ptr->get_reps()) {
                ins_gene_ptr_->prepare_const_value(rep_in_block.get(), trans_dep, curr_kernel_.get());
            }
        }
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_mgr::alloc_res(asc_ccu_res_snapshot& res_pack)
{
    asc_ccu_res_request res_req = curr_kernel_->get_resource_request();
    CCU_CHK_RET(set_instr_request(curr_kernel_, res_req));
    asc_ccu_res_repository remaining{};
    asc_ccu_res_repository allocated{};
    CcuResult plan_ret = plan_kernel_resources(res_pack.get_ccu_res_repo(), res_req, remaining, allocated);
    if (plan_ret != CcuResult::CCU_SUCCESS) {
        dump_res_req_info(res_req);
        HCCL_WARNING("[CcuKernelMgr][%s] resource is not enough.", __func__);
        return plan_ret;
    }

    CCU_CHK_RET(set_kernel_instruction_resource(curr_kernel_, allocated));

    res_pack.get_ccu_res_repo() = std::move(remaining);
    curr_kernel_->set_res_repository(std::move(allocated));
    const HcommCcuDieMetadataPod* metadata = res_pack.get_die_metadata(curr_kernel_->get_die_id());
    CCU_CHK_PTR_NULL(metadata);
    curr_kernel_->set_mission_key(metadata->missionKey);

    return CcuResult::CCU_SUCCESS;
}

template <typename t1, typename t2>
HcclResult reset_rep_resource_template(
    std::vector<t1>& resource, const std::vector<t2>& repository, const uint32_t start_index = 0)
{
    if (resource.size() > repository.size() - start_index) {
        HCCL_ERROR(
            "[CcuKernelMgr][ResetRepResourceTemplate]resource size[%u] bigger "
            "repository size[%u] typeid[%s]",
            resource.size(), repository.size(), typeid(t1).name());
        return HcclResult::HCCL_E_INTERNAL;
    }

    for (uint32_t j = 0; j < resource.size(); j++) {
        resource[j].reset(repository[j + start_index].startId);
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult reset_rep_resource_to_res_repository(
    ccu_rep_resource& total_rep_res, const asc_ccu_res_repository& total_res_repository)
{
    // 遍历translatorRepRes, 将每个rep的虚拟资源翻译到实际物理资源上
    for (uint32_t i = 0; i < ccu_max_iodie_num; i++) {
        CHK_RET(reset_rep_resource_template(total_rep_res.ccubufs[i], total_res_repository.ms[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.block_ccubufs[i], total_res_repository.block_ms[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.executor[i], total_res_repository.loop_engine[i]));
        CHK_RET(
            reset_rep_resource_template(total_rep_res.block_executor[i], total_res_repository.block_loop_engine[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.completed_event[i], total_res_repository.cke[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.block_completed_event[i], total_res_repository.block_cke[i]));
        CHK_RET(reset_rep_resource_template(
            total_rep_res.local_notify[i], total_res_repository.block_cke[i],
            total_rep_res.block_completed_event[i].size())); // 两类资源都使用block cke，需要调整起始分配位置
        CHK_RET(reset_rep_resource_template(total_rep_res.address[i], total_res_repository.gsa[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.block_address[i], total_res_repository.block_gsa[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.variable[i], total_res_repository.xn[i]));
        CHK_RET(reset_rep_resource_template(total_rep_res.continuous_variable[i], total_res_repository.block_xn[i]));
    }
    return HcclResult::HCCL_SUCCESS;
}

using die_res_infos = std::array<std::vector<HcommCcuResRangePod>, ccu_max_iodie_num>;
static HcclResult save_kernel_mission_info(ccu_kernel* kernel, const die_res_infos& mission_id)
{
    const uint32_t die_id = kernel->get_die_id();
    // 从missionId中获取一个元素并从missionId中删除，当前应只有一个元素，且无需删除
    if (mission_id[die_id].empty()) {
        HCCL_ERROR("[%s] failed, dieId[%u] does not have missions.", __func__, die_id);
        return HcclResult::HCCL_E_INTERNAL;
    }

    kernel->set_mission_id(mission_id[die_id].back().startId);
    return HcclResult::HCCL_SUCCESS;
}

static void dump_res_repository_info(const asc_ccu_res_repository& res_repo)
{
    for (uint32_t i = 0; i < ccu_max_iodie_num; i++) {
        if (res_repo.ms[i].size() != 0 || res_repo.block_ms[i].size() != 0 || res_repo.cke[i].size() != 0 ||
            res_repo.block_cke[i].size() != 0 || res_repo.loop_engine[i].size() != 0 ||
            res_repo.block_loop_engine[i].size() != 0 || res_repo.gsa[i].size() != 0 ||
            res_repo.block_gsa[i].size() != 0 || res_repo.xn[i].size() != 0 || res_repo.block_xn[i].size() != 0 ||
            res_repo.mission[i].size() != 0 || res_repo.instruction[i].size() != 0) {
            HCCL_INFO(
                "DumpResRepository: dieId[%u], ms size[%u], blockMs size[%u], cke size[%u], blockCke size[%u], "
                "loopEngine size[%u], blockLoopEngine size[%u], gsa size[%u], blockGsa size[%u], xn size[%u], "
                "blockXn size[%u], mission size[%u], instruction size[%u]",
                i, res_repo.ms[i].size(), res_repo.block_ms[i].size(), res_repo.cke[i].size(),
                res_repo.block_cke[i].size(), res_repo.loop_engine[i].size(), res_repo.block_loop_engine[i].size(),
                res_repo.gsa[i].size(), res_repo.block_gsa[i].size(), res_repo.xn[i].size(),
                res_repo.block_xn[i].size(), res_repo.mission[i].size(), res_repo.instruction[i].size());
        }
    }
}

inline void expand_res_info(
    std::vector<HcommCcuResRangePod>& expend_res_infos, const std::vector<HcommCcuResRangePod>& res_infos)
{
    // 将resInfo中的资源信息还原为单个资源粒度
    for (auto& res_info : res_infos) {
        for (uint32_t id = 0; id < res_info.count; id++) {
            expend_res_infos.push_back({res_info.resourceType, res_info.dieId, res_info.startId + id, 1});
        }
    }
}

static CcuResult expand_res_repo(asc_ccu_res_repository& total_res, const asc_ccu_res_repository& tmp_res_repository)
{
    // 合并获取的所持有的资源信息, 按照类型合并资源总和到totalRes中
    for (uint32_t i = 0; i < ccu_max_iodie_num; i++) {
        expand_res_info(total_res.ms[i], tmp_res_repository.ms[i]);
        expand_res_info(total_res.block_ms[i], tmp_res_repository.block_ms[i]);
        expand_res_info(total_res.loop_engine[i], tmp_res_repository.loop_engine[i]);
        expand_res_info(total_res.block_loop_engine[i], tmp_res_repository.block_loop_engine[i]);
        expand_res_info(total_res.cke[i], tmp_res_repository.cke[i]);
        expand_res_info(total_res.block_cke[i], tmp_res_repository.block_cke[i]);
        expand_res_info(total_res.gsa[i], tmp_res_repository.gsa[i]);
        expand_res_info(total_res.block_gsa[i], tmp_res_repository.block_gsa[i]);
        expand_res_info(total_res.xn[i], tmp_res_repository.xn[i]);
        expand_res_info(total_res.block_xn[i], tmp_res_repository.block_xn[i]);
        expand_res_info(total_res.mission[i], tmp_res_repository.mission[i]);
        expand_res_info(total_res.instruction[i], tmp_res_repository.instruction[i]);
    }
    dump_res_repository_info(total_res);
    return CcuResult::CCU_SUCCESS;
}

template <typename t>
static HcclResult merge_exported_resources(
    const std::unordered_map<std::string, t>& input_res, std::unordered_map<std::string, t>& output_res)
{
    for (const auto& item : input_res) {
        const auto& res_tag = item.first;
        if (output_res.find(res_tag) != output_res.end()) {
            HCCL_ERROR(
                "[CcuKernelMgr][%s] failed, exported resource tag[%s] is already existed, "
                "please check.",
                __func__, res_tag);
            return HcclResult::HCCL_E_PARA;
        }

        output_res.insert(item);
    }

    return HcclResult::HCCL_SUCCESS;
}

template <typename t>
static HcclResult reset_imported_resources(
    std::unordered_map<std::string, t>& imported_res, const std::unordered_map<std::string, t>& exported_res)
{
    for (auto& item : imported_res) {
        const auto& res_tag = item.first;
        const auto& iter = exported_res.find(res_tag);
        if (iter == exported_res.end()) {
            HCCL_ERROR("[CcuKernelMgr][%s] failed to find exported resources by tag[%s].", __func__, res_tag.c_str());
            return HcclResult::HCCL_E_NOT_FOUND;
        }

        item.second.reset(iter->second.id(), iter->second.die_id());
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult process_inter_ctx_res(const std::vector<ccu_kernel*>& kernels)
{
    std::unordered_map<std::string, ccu_rep::local_notify> total_exported_notifies;

    for (const auto kernel : kernels) {
        const auto& exported_res = kernel->get_exported_res();
        CHK_RET(merge_exported_resources(exported_res.shared_notifies, total_exported_notifies));
    }

    for (auto kernel : kernels) {
        auto& imported_res = kernel->get_imported_res();
        CHK_RET(reset_imported_resources(imported_res.shared_notifies, total_exported_notifies));
    }

    return HcclResult::HCCL_SUCCESS;
}

static HcclResult trans_rep_res_to_phy_res(const std::vector<ccu_kernel*>& kernels)
{
    for (auto kernel : kernels) {
        const auto& total_res_repository = kernel->get_res_repository();
        auto& total_rep_res = kernel->get_resource();

        // 将ccu kernel持有的物理资源赋给资源对象
        asc_ccu_res_repository expanded_res_repo{};
        expand_res_repo(expanded_res_repo, total_res_repository);
        CHK_RET(reset_rep_resource_to_res_repository(total_rep_res, expanded_res_repo));

        CHK_RET(save_kernel_mission_info(kernel, total_res_repository.mission));
    }

    CHK_RET(process_inter_ctx_res(kernels));

    return HcclResult::HCCL_SUCCESS;
}

CcuResult ccu_kernel_mgr::translate(const std::vector<ccu_kernel_handle>& kernel_handles)
{
    if (kernel_handles.empty()) {
        HCCL_INFO("[CcuKernelMgr][%s] passed, kernelHandles are empty.", __func__);
        return CcuResult::CCU_SUCCESS;
    }

    std::vector<ccu_kernel*> kernels{};
    std::unique_lock<std::mutex> map_lock(kernel_map_mutex_);
    for (const auto kernel_handle : kernel_handles) {
        const auto& iter = kernel_map_.find(kernel_handle);
        if (iter == kernel_map_.end()) {
            HCCL_ERROR(
                "[CcuKernelMgr][%s] failed to find kernel by ccu kernel handle[0x%llx].", __func__, kernel_handle);
            return CcuResult::CCU_E_NOT_FOUND;
        }

        kernels.push_back(iter->second.get());
    }
    map_lock.unlock();

    constexpr bool is_func_block = false; // 当前不支持MC2

    std::unique_lock<std::mutex> translate_lock(translate_mutex_);
    CCU_CHK_RET(trans_rep_res_to_phy_res(kernels));
    CCU_CHK_RET(trans_rep_sequence_to_microcode(kernels, is_func_block));

    for (auto& reference_mgr_map : reference_mgrs_) {
        for (auto& reference_mgr : reference_mgr_map.second) {
            reference_mgr.second->clear_rep_reference();
        }
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_mgr::un_register(const ccu_kernel_handle kernel_handle)
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);

    // 校验kernelMap_中是否存在executorId对应的kernel
    auto it = kernel_map_.find(kernel_handle);
    CHK_PRT_RET(
        it == kernel_map_.end(),
        HCCL_ERROR("[CcuKernelMgr][%s] kernelHandle [%llu] does not exist", __func__, kernel_handle),
        CcuResult::CCU_E_NOT_FOUND);

    CCU_CHK_RET(clear_kernel_instruction_resource(*it->second));
    kernel_map_.erase(kernel_handle);
    return CcuResult::CCU_SUCCESS;
}

HcclResult ccu_kernel_mgr::instantiation_translator(const uint16_t die_id, asc_ccu_res_snapshot& res_pack)
{
    const HcommCcuDieMetadataPod* metadata = res_pack.get_die_metadata(die_id);
    CHK_PTR_NULL(metadata);
    // 临时申请device hbm内存用于查询token信息，由本地 RAII 对象管理生命周期
    ccu_dev_mem tmp_dev_mem{1};
    if (!tmp_dev_mem.valid()) {
        HCCL_ERROR("[InstantiationTranslator] alloc temp device memory for token query failed.");
        return HcclResult::HCCL_E_MEMORY;
    }
    auto hbm_token_info = asc::ccu_rep::get_token_info(tmp_dev_mem.get_addr(), tmp_dev_mem.get_size());
    ccu_rep::trans_dep trans_dep{};
    trans_dep.logical_id = res_pack.get_device_logic_id();
    trans_dep.die_id = die_id;
    trans_dep.reserve_channal_id[0] = static_cast<uint16_t>(metadata->innerDieLoopChannelId);
    trans_dep.reserve_channal_id[1] = static_cast<uint16_t>(metadata->interDieLoopChannelId);
    for (uint32_t metadata_die = 0; metadata_die < HCOMM_CCU_MAX_DIE_NUM; ++metadata_die) {
        const HcommCcuDieMetadataPod* die_metadata = res_pack.get_die_metadata(metadata_die);
        if (die_metadata != nullptr) {
            trans_dep.xn_base_addr[metadata_die] = die_metadata->xnBaseAddr;
        }
    }
    trans_dep.ccu_res_space_token_info =
        ccu_rep::get_token(metadata->resourceSpaceTokenId, metadata->resourceSpaceTokenValue, 1);
    trans_dep.mem_token_info = hbm_token_info;

    // 实例化CcuRepReferenceManager和CcuRepTranslator，并为CcuRepReferenceManager绑定物理资源
    for (uint32_t i = 0; i < ccu_rep::ccu_translator_mission_slot_num; i++) {
        reference_mgrs_[die_id][i] = std::make_shared<asc::ccu_rep::ccu_rep_reference_manager>(die_id);
        translators_[die_id][i] =
            std::make_shared<asc::ccu_rep::ccu_rep_translator>(reference_mgrs_[die_id][i], trans_dep);
    }

    ccu_rep_resource translator_rep_res;
    for (uint32_t i = 0; i < ccu_rep::ccu_translator_mission_slot_num; i++) {
        reference_mgrs_[die_id][i]->get_res(translator_rep_res);
        translators_[die_id][i]->get_res(translator_rep_res);
    }

    asc_ccu_res_request request{};
    CcuResult ccu_ret = build_translator_resource_request(die_id, ccu_version_, request);
    if (ccu_ret != CcuResult::CCU_SUCCESS) {
        return static_cast<HcclResult>(ccu_ret);
    }

    asc_ccu_res_repository remaining_repository;
    asc_ccu_res_repository translator_res_repository;
    ccu_ret =
        plan_kernel_resources(res_pack.get_ccu_res_repo(), request, remaining_repository, translator_res_repository);
    if (ccu_ret != CcuResult::CCU_SUCCESS) {
        dump_res_req_info(request);
        HCCL_WARNING("[CcuKernelMgr][%s] translator resource is not enough, dieId[%u].", __func__, die_id);
        return static_cast<HcclResult>(ccu_ret);
    }
    asc_ccu_res_repository expanded_translator_res_repository;
    ccu_ret = expand_res_repo(expanded_translator_res_repository, translator_res_repository);
    if (ccu_ret != CcuResult::CCU_SUCCESS) {
        return static_cast<HcclResult>(ccu_ret);
    }
    // 将translator中的rep虚拟资源按类型进行和CCU物理资源映射
    HcclResult hccl_ret = reset_rep_resource_to_res_repository(translator_rep_res, expanded_translator_res_repository);
    if (hccl_ret != HcclResult::HCCL_SUCCESS) {
        return hccl_ret;
    }
    res_pack.get_ccu_res_repo() = std::move(remaining_repository);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel_mgr::load_instruction(const ccu_rep::ccu_instr_info& instr_info, const uint32_t die_id)
{
    const uint64_t instr_info_size = instr_info.instr_vec.size() * sizeof(asc::ccu_rep::ccu_instr);

    if (instr_info_size == 0) {
        return HcclResult::HCCL_E_PARA;
    }
    if (instruction_load_dev_mem_size_ < instr_info_size) {
        if (instruction_load_dev_mem_ != nullptr) {
            CHK_RET(static_cast<HcclResult>(aclrtFree(instruction_load_dev_mem_)));
            instruction_load_dev_mem_ = nullptr;
            instruction_load_dev_mem_size_ = 0;
        }
        CHK_RET(static_cast<HcclResult>(
            aclrtMalloc(&instruction_load_dev_mem_, instr_info_size, ACL_MEM_MALLOC_HUGE_FIRST)));
        instruction_load_dev_mem_size_ = instr_info_size;
    }

    CHK_RET(static_cast<HcclResult>(aclrtMemcpy(
        instruction_load_dev_mem_, instr_info_size, instr_info.instr_vec.data(), instr_info_size,
        ACL_MEMCPY_HOST_TO_DEVICE)));

    // 只传语义参数；opcode 选择、TLV 报文组装与驱动交互均由 hcomm 在其 SO 内完成
    HcommCcuInstructionLoadPod request{};
    request.header.version = HCOMM_CCU_INSTRUCTION_ABI_VERSION;
    request.header.magicWord = HCOMM_CCU_INSTRUCTION_LOAD_MAGIC_WORD;
    request.header.size = sizeof(request);
    request.deviceLogicId = dev_logic_id_;
    request.dieId = die_id;
    request.startInstructionId = instr_info.start_instr_id;
    request.deviceAddress = reinterpret_cast<uint64_t>(instruction_load_dev_mem_);
    request.byteSize = instr_info_size;

    const auto load_instruction = get_current_ccu_control_ops().loadInstruction;
    CHK_PRT_RET(
        load_instruction == nullptr, HCCL_ERROR("[CcuKernelMgr][%s] controlOps.loadInstruction is nullptr.", __func__),
        HcclResult::HCCL_E_INTERNAL);
    auto ret = static_cast<HcclResult>(load_instruction(&request));
    if (ret != HcclResult::HCCL_SUCCESS) {
        HCCL_ERROR(
            "[CcuKernelMgr][%s] failed to load instruction, "
            "devLogicId[%d] dieId[%u] startInstrId[%u] byteSize[%llu] ret[%d].",
            __func__, dev_logic_id_, die_id, instr_info.start_instr_id,
            static_cast<unsigned long long>(instr_info_size), ret);
        return ret;
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult ccu_kernel_mgr::trans_rep_sequence_to_microcode(const std::vector<ccu_kernel*>& kernels, bool is_func_block)
{
    for (auto kernel : kernels) {
        const uint32_t die_id = kernel->get_die_id();
        const uint32_t mission_id = kernel->get_mission_id();

        try {
            const auto& instr_info = translators_[die_id][mission_id]->translate(
                kernel, kernel->get_rep_sequence(), kernel->get_instr_id(), is_func_block);
            const size_t region_size = compute_kernel_instr_region_size(*kernel);
            CHK_PRT_RET(
                instr_info.instr_vec.size() > region_size,
                HCCL_ERROR(
                    "[CcuKernelMgr][%s] optimized instruction count[%zu] exceeds reserved region[%zu].", __func__,
                    instr_info.instr_vec.size(), region_size),
                HcclResult::HCCL_E_INTERNAL);
            CHK_RET(load_instruction(instr_info, die_id));
            kernel->set_ccu_instr_info(instr_info); // 指令下发成功后可以对kernel进行launch
        } catch (const ::AscendC::ccu::detail::ccu_exception& exception) {
            HCCL_ERROR("[%s] exception: %s", __func__, exception.what());
            return static_cast<HcclResult>(exception.code());
        } catch (const std::out_of_range& exception) {
            HCCL_ERROR("[%s] out of range: %s", __func__, exception.what());
            return HcclResult::HCCL_E_NOT_FOUND;
        } catch (const std::runtime_error& exception) {
            HCCL_ERROR("[%s] runtime error: %s", __func__, exception.what());
            return HcclResult::HCCL_E_RUNTIME;
        } catch (const std::logic_error& exception) {
            HCCL_ERROR("[%s] logic error: %s", __func__, exception.what());
            return HcclResult::HCCL_E_INTERNAL;
        } catch (const std::exception& exception) {
            HCCL_ERROR("[%s] exception: %s", __func__, exception.what());
            return HcclResult::HCCL_E_INTERNAL;
        } catch (...) {
            HCCL_ERROR("[%s] unknown exception", __func__);
            return HcclResult::HCCL_E_INTERNAL;
        }
    }

    return HcclResult::HCCL_SUCCESS;
}

ccu_kernel* ccu_kernel_mgr::get_kernel(const ccu_kernel_handle kernel_handle)
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    auto it = kernel_map_.find(kernel_handle);
    if (it == kernel_map_.end()) {
        HCCL_ERROR("[CcuKernelMgr][%s] handle[%llx] is not existed.", __func__, kernel_handle);
        return nullptr;
    }

    return it->second.get();
}

CcuResult ccu_kernel_mgr::get_ccu_kernel_info(const ccu_kernel_handle kernel_handle, ccu_kernel_info& info)
{
    std::unique_lock<std::mutex> lock(kernel_map_mutex_);
    auto it = kernel_map_.find(kernel_handle);
    if (it == kernel_map_.end()) {
        HCCL_ERROR("[CcuKernelMgr][%s] handle[%llx] is not existed.", __func__, kernel_handle);
        return CcuResult::CCU_E_NOT_FOUND;
    }
    CCU_CHK_RET(it->second->get_ccu_kernel_info(info));
    return CcuResult::CCU_SUCCESS;
}

ccu_kernel* ccu_kernel_mgr::get_current_kernel() { return curr_kernel_.get(); }

} // namespace asc
