/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_KERNEL_H
#define ASCCOMM_CCU_KERNEL_H

#include <cstdint>
#include <functional>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "hcomm/resource/kernel/ccu_task_arg_v1.h"
#include "hcomm/resource/kernel/ccu_task_param_v1.h"

#include "hcomm/resource/common/ccu_kernel_resource.h"
#include "hcomm/resource/common/asc_ccu_resource_local.h"
#include "hcomm/resource/microcode/ccu_instr_info_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

#include "hcomm/resource/representation/interface/ccu_funccall_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopcall_v1.h"

// 类型（HcclResult 等）统一取自 hcomm 包内 hcomm_types.h，避免耦合 hccl 私有定义
#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/hcomm_types.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"

// 暂时引用方便算法开发
#include "hcomm/resource/representation/interface/ccu_repeat_v1.h"
#include "hcomm/resource/representation/interface/ccu_condition_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopcall_v1.h"
#include "hcomm/resource/microcode/ccu_assist_pub.h"

#ifndef CCU_PROFILING // 和hccl仓兼容性使用
#define CCU_PROFILING
#endif
#include "ccu/hcomm/ccu_api_types.h"

namespace asc {

struct group_info {
    uint16_t loop_param_id;
    uint16_t parallel_param_id;
    uint16_t residual_id;
};

struct group_op_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

struct ccu_kernel_info {
    uint32_t max_task_args_num{0};
};

class ccu_kernel : public ccu_rep::ccu_rep_context {
public:
    ccu_kernel() = default;
    ~ccu_kernel() override;

    HcclResult setup_profiling_info(const char* kernel_func_name);
    HcclResult apply_die_from_channels(uint32_t valid_die_mask);
    HcclResult validate_and_apply_die(uint32_t target_die_id, uint32_t valid_die_mask);

    asc_ccu_res_request get_resource_request();
    asc_ccu_res_repository& get_res_repository();
    ccu_rep_resource& get_resource();
    ccu_shared_resource& get_exported_res();
    ccu_shared_resource& get_imported_res();

    void set_res_repository(const asc_ccu_res_repository& res_repo);
    void set_res_repository(asc_ccu_res_repository&& res_repo);
    void set_instr_id(uint32_t instr_id);
    uint32_t get_instr_id() const;
    uint32_t get_instr_count();
    uint32_t get_rep_need_to_add_latency() const;
    void set_ccu_instr_info(const ccu_rep::ccu_instr_info& instr_info);
    void set_instruction_resource(const HcommCcuResRangePod& range);
    bool get_instruction_resource(HcommCcuResRangePod& range) const;
    void clear_instruction_resource();

    CcuResult gene_task_params(const uint64_t* task_args, uint32_t arg_num, std::vector<ccu_task_param>& task_params);

    void set_ins_generater(ccu_rep::ccu_ins_generater_base* ins_generater_base);
    void set_ccu_version(uint32_t version) { ccu_version_ = version; }
    // 该友元函数用于在context类外创建Variable并被context内的资源管理器管理
    friend ccu_rep::variable ccu_rep::create_variable(ccu_rep::ccu_rep_context* context);

    HcclResult add_profiling_info(
        const ChannelHandle* channels, uint32_t channel_num, HcclDataType data_type, HcclDataType output_data_type,
        HcclReduceOp op_type, const std::string& op_name);

    HcclResult add_ccu_profiling(
        group_info group_info, const std::vector<ChannelHandle> channel_handle, HcclDataType data_type,
        HcclDataType output_data_type, HcclReduceOp op_type, const std::string& op_name);
    HcclResult add_ccu_profiling(
        const ChannelHandle* channels, uint32_t channel_num, HcclDataType data_type, HcclDataType output_data_type,
        HcclReduceOp op_type, const std::string& op_name);
    HcclResult get_ccu_profiling_info(
        const uint64_t* task_args, uint32_t arg_size, std::vector<ccu_profiling_info>& all_ccu_profiling_info);

    const std::vector<ccu_profiling_info>& get_all_ccu_profiling_info() { return all_ccu_profiling_infos_; };

    // process const values
    std::unordered_map<uint64_t, ccu_rep::variable>& get_const_value2_var_map() { return const_value2_var_map_; }
    HcclResult add2_const_value2_var_map(std::vector<uint64_t>& values);

    const std::unordered_set<ChannelHandle>& get_channels() { return channels_; }

public:
    // Alloc 相关接口
    CcuResult variable_alloc(ccu_variable_handle* var_handle);
    CcuResult address_alloc(ccu_address_handle* addr_handle);
    CcuResult event_alloc(ccu_event_handle* event_handle);
    CcuResult buffer_alloc(ccu_buffer_handle* buf_handle);
    CcuResult local_addr_alloc(
        ccu_local_addr_handle* local_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle);
    CcuResult remote_addr_alloc(
        ccu_remote_addr_handle* remote_addr_handle, ccu_address_handle* addr_handle, ccu_variable_handle* token_handle);
    CcuResult block_variable_alloc(ccu_variable_handle* var_handles, uint32_t count);
    CcuResult block_event_alloc(ccu_event_handle* event_handles, uint32_t count);
    CcuResult block_buffer_alloc(ccu_buffer_handle* buf_handles, uint32_t count);
    CcuResult variable_create_by_channel(ChannelHandle channel, uint32_t var_index, ccu_variable_handle* var_handle);
    CcuResult variable_create_by_acquire(
        ccu_variable_handle acq_handle, uint32_t index, ccu_variable_handle* var_handle);
    CcuResult event_create_by_acquire(ccu_event_handle acq_handle, uint32_t index, ccu_event_handle* event_handle);

    // 参数加载类 相关接口
    CcuResult load_arg(ccu_variable_handle var_handle, uint32_t arg_id);
    CcuResult get_ccu_kernel_info(ccu_kernel_info& info) const;
    CcuResult load_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num);
    CcuResult ccu_load_var_from_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num);
    CcuResult store_var(uint64_t addr, ccu_variable_handle var_handle, uint32_t num);
    CcuResult ccu_store_var_to_var_addr(ccu_variable_handle addr_handle, ccu_variable_handle var_handle, uint32_t num);

    // Event信号同步类 相关接口
    //  mask 由调用方独立传入（与 Event 句柄解耦），不再设独立的 SetEventMask 接口。
    CcuResult event_record(ccu_event_handle event_handle, uint32_t mask);
    CcuResult event_wait(ccu_event_handle event_handle, uint32_t mask);
    CcuResult notify_record(const ChannelHandle channel, uint32_t remote_notify_idx, uint32_t mask);
    CcuResult notify_wait(const ChannelHandle channel, uint32_t local_notify_idx, uint32_t mask);
    CcuResult write_variable_with_notify(
        const ChannelHandle channel, ccu_variable_handle var_handle, uint32_t remote_var_idx,
        uint32_t remote_notify_idx, uint32_t mask);
    // 本地（同 device 内跨 core）通知同步：用 notifyTag 字符串作为对端标识，
    // 由调用方约定生产者/消费者使用相同的 tag 字符串完成配对。
    // 与 NotifyRecord/Wait（用 ChannelHandle 标识跨 rank 通道）的对偶。
    // 必须 public：C API ccu_primitives_impl.cc 直接调用。
    CcuResult local_notify_record(const char* notify_tag, const uint32_t mask);
    CcuResult local_notify_wait(const char* notify_tag, const uint32_t mask);
    // 本地数据拷贝 相关接口
    CcuResult local_copy_mem_to_buffer(
        ccu_buffer_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, uint32_t mask);
    CcuResult local_copy_buffer_to_mem(
        ccu_local_addr_handle dst_handle, ccu_buffer_handle src_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, uint32_t mask);
    CcuResult local_copy_mem_to_mem(
        ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, uint32_t mask);

    // 本地reduce 相关接口
    CcuResult local_mem_reduce(
        ccu_local_addr_handle dst_handle, ccu_local_addr_handle src_handle, ccu_variable_handle len_handle,
        HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle, uint32_t mask);
    CcuResult local_buffer_reduce(
        ccu_buffer_handle* buf_handles, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
        HcclReduceOp op_type, ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask);

    // 运算重载 相关接口
    CcuResult variable_assign_imm(ccu_variable_handle var, uint64_t immediate);
    CcuResult variable_assign_var(ccu_variable_handle var, ccu_variable_handle var_a);
    CcuResult variable_add_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_sub_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_mul_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_add_imm_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate);
    CcuResult variable_sub_imm_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate);
    CcuResult variable_mul_imm_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, uint16_t immediate);
    CcuResult variable_and_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_or_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_xor_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_not_var(ccu_variable_handle var_handle, ccu_variable_handle var_a_handle);
    CcuResult variable_shl_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult variable_shr_var_to_var(
        ccu_variable_handle var_handle, ccu_variable_handle var_a_handle, ccu_variable_handle var_b_handle);
    CcuResult address_assign_imm(ccu_address_handle addr, uint64_t immediate);
    CcuResult address_assign_var(ccu_address_handle addr_handle, ccu_variable_handle var_handle);
    CcuResult address_assign_addr(ccu_address_handle dst_addr_handle, ccu_address_handle src_addr_handle);
    CcuResult address_add_var_to_addr(
        ccu_address_handle res_addr, ccu_address_handle lhs_addr, ccu_variable_handle rhs_var);
    CcuResult address_add_addr_to_addr(
        ccu_address_handle res_addr_handle, ccu_address_handle addr_a_handle, ccu_address_handle addr_b_handle);
    CcuResult address_add_assign_var(ccu_address_handle addr, ccu_variable_handle var);
    CcuResult address_add_assign_addr(ccu_address_handle addr_handle, ccu_address_handle other_handle);
    CcuResult address_add_imm_to_addr(ccu_address_handle res_addr, ccu_address_handle addr_a, uint16_t imm);

    // 远端数据传输操作

    CcuResult read_mem_to_mem(
        ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
        ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask);
    CcuResult read_mem_to_buffer(
        ChannelHandle channel, ccu_buffer_handle local_handle, ccu_remote_addr_handle remote_handle,
        ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask);
    CcuResult read_mem_to_mem_reduce(
        ChannelHandle channel, ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle,
        ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
        uint32_t mask);
    CcuResult write_mem_to_mem(
        ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
        ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask);
    CcuResult write_buffer_to_mem(
        ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_buffer_handle local_handle,
        ccu_variable_handle len_handle, ccu_event_handle event_handle, uint32_t mask);
    CcuResult write_mem_to_mem_reduce(
        ChannelHandle channel, ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle,
        ccu_variable_handle len_handle, HcclDataType data_type, HcclReduceOp op_type, ccu_event_handle event_handle,
        uint32_t mask);

    CcuResult if_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label);
    CcuResult if_begin_var(
        ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type,
        const char* label);
    CcuResult if_else(const char* label);
    CcuResult if_end(const char* label);

    void if_label_stack_push(const char* label);
    void if_label_stack_mark_body_done();
    const char* if_label_stack_pop_for_else();
    bool if_label_stack_top_is_closable();
    const char* if_label_stack_pop();

    void do_while_label_stack_push(const char* label);
    const char* do_while_label_stack_pop_for_while();

    void flush_closable_pending_ifs();
    void append(std::shared_ptr<ccu_rep::ccu_rep_base> rep) override;
    CcuResult while_begin(ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label);
    CcuResult while_begin_var(
        ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type,
        const char* label);
    CcuResult while_end(const char* label);
    CcuResult do_while_begin(const char* label);
    CcuResult do_while_end(
        ccu_variable_handle var, uint64_t immediate, ccu_condition_type cond_type, const char* label);
    CcuResult do_while_end_var(
        ccu_variable_handle lhs_handle, ccu_variable_handle rhs_handle, ccu_condition_type cond_type,
        const char* label);

    CcuResult loop_create(ccu_loop* loop);
    CcuResult loop_body_enter(ccu_loop loop);
    CcuResult loop_body_exit(ccu_loop loop);
    CcuResult loop_group_create(ccu_loop_group* group, uint32_t max_loop_num, const ccu_loop_group_config* config);
    CcuResult loop_group_create_from_var(
        ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var_handle,
        ccu_variable_handle offset_var_handle);
    CcuResult loop_group_create_from_var_v2(
        ccu_loop_group* group, uint32_t max_loop_num, ccu_variable_handle parallel_var_v2,
        ccu_variable_handle offset_var_v2, ccu_variable_handle var_offset_var);
    CcuResult loop_group_add_loop(ccu_loop_group group, ccu_loop loop, const ccu_loop_config* config);
    CcuResult loop_group_add_loop_from_var(ccu_loop_group group, ccu_loop loop, ccu_variable_handle loop_param_var);
    CcuResult loop_group_add_loop_from_var_v2(
        ccu_loop_group group, ccu_loop loop, ccu_variable_handle iter_num_var, ccu_variable_handle addr_offset_var,
        ccu_variable_handle ctx_id_var);

    CcuResult func_block_lookup(const void* func_ptr, uint64_t* out_handle);
    CcuResult func_block_begin(const void* func_ptr, uint64_t* out_handle);
    CcuResult func_block_end(uint64_t handle);
    CcuResult func_define_in_arg(uint64_t handle, ccu_variable_handle formal);
    CcuResult func_call(uint64_t handle, const ccu_variable_handle* in_args, uint32_t num_in);

private:
    CcuResult get_variable_by_handle(ccu_variable_handle var_handle, ccu_rep::variable** variable);
    CcuResult get_event_by_handle(ccu_event_handle event_handle, ccu_rep::completed_event** event);
    CcuResult latch_body_error(CcuResult err);
    // 按需扩容 res_.blockExecutor[0]：不足 maxLoopNum 时补足，足够则不动；
    // 由 LoopGroupCreate / LoopGroupCreateFromVar 在 LoopGroup 创建时调用。
    CcuResult ensure_loop_engine_pool(uint32_t max_loop_num);

    CcuResult validate_task_args(const uint64_t* task_args, uint32_t args_num) const;
    void fill_task_param(
        ccu_task_param& param, uint32_t index, uint32_t seq_num, const uint64_t* task_args, uint32_t args_num) const;

    CcuResult resolve_buf_remote_len_event(
        ccu_buffer_handle buf_handle, ccu_remote_addr_handle remote_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, ccu_rep::ccu_buf** buf, ccu_rep::remote_addr** remote, ccu_rep::variable** len,
        ccu_rep::completed_event** event);

    CcuResult resolve_local_remote_len_event(
        ccu_local_addr_handle local_handle, ccu_remote_addr_handle remote_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, ccu_rep::local_addr** local, ccu_rep::remote_addr** remote,
        ccu_rep::variable** len, ccu_rep::completed_event** event);

    CcuResult resolve_remote_local_len_event(
        ccu_remote_addr_handle remote_handle, ccu_local_addr_handle local_handle, ccu_variable_handle len_handle,
        ccu_event_handle event_handle, ccu_rep::remote_addr** remote, ccu_rep::local_addr** local,
        ccu_rep::variable** len, ccu_rep::completed_event** event);

    // 校验从 varHandle 起的 num 个 Variable 句柄对应的内部变量 Id 连续递增，
    // 用于 LoadVar/StoreVar 等接口对“连续变量块”的前置校验。
    CcuResult check_continuous_variables(
        ccu_variable_handle var_handle, uint32_t num, const ccu_rep::variable& base_var, const char* tag);

    // GetCcuProfilingInfo 的子步骤：处理 sqe & waitcke 类型的 profiling 信息，结果直接 push 到 allCcuProfilingInfos_
    // 中。
    HcclResult collect_sqe_and_wait_cke_profiling_info();
    // GetCcuProfilingInfo 的子步骤：根据 LoopGroup 的 profiling 缓存构建
    // varId -> argIndex 与 varId -> varId 两个查找表，供 LoopGroup 段查询入参使用。
    HcclResult build_loop_group_var_id_maps(
        std::unordered_map<uint16_t, uint32_t>& var_id2_arg_index_map,
        std::unordered_map<uint16_t, uint16_t>& var_id2_var_id_map);
    // GetCcuProfilingInfo 的子步骤：处理 LoopGroup 的 profiling 信息，结果 push 到 allCcuProfilingInfos_ 中。
    HcclResult collect_loop_group_profiling_info(
        const uint64_t* task_args, uint32_t arg_size,
        const std::unordered_map<uint16_t, uint32_t>& var_id2_arg_index_map,
        const std::unordered_map<uint16_t, uint16_t>& var_id2_var_id_map);

    struct if_label_entry {
        const char* label{nullptr};
        bool body_done{false};
    };
    struct do_while_label_entry {
        const char* label{nullptr};
        std::shared_ptr<ccu_rep::ccu_rep_block> snapshot_block{nullptr};
        size_t snapshot_rep_count{0};
    };
    std::vector<if_label_entry> iflabel_stack_;
    std::vector<do_while_label_entry> do_while_label_stack_;
    bool is_flushing_ = false;

    struct pending_if_context {
        std::shared_ptr<ccu_rep::ccu_rep_jump_label> else_label;
        std::shared_ptr<ccu_rep::ccu_rep_jump_label> end_label;
        bool has_else{false};
    };

    struct pending_while_context {
        std::shared_ptr<ccu_rep::ccu_rep_jump_label> begin_label;
        std::shared_ptr<ccu_rep::ccu_rep_jump_label> end_label;
        ccu_variable_handle var_handle;
        uint64_t immediate;
        ccu_condition_type cond_type;
    };

    struct pending_do_while_context {
        std::shared_ptr<ccu_rep::ccu_rep_jump_label> begin_label;
    };

    std::unordered_map<ccu_variable_handle, ccu_rep::variable> ccu_var_map_{};

    std::unordered_map<std::string, pending_if_context> pending_if_ctx_{};
    std::unordered_map<std::string, pending_while_context> pending_while_ctx_{};
    std::unordered_map<std::string, pending_do_while_context> pending_do_while_ctx_{};

    std::unordered_map<ccu_event_handle, ccu_rep::completed_event> ccu_event_map_{};

    CcuResult get_buffer_by_handle(ccu_buffer_handle buffer_handle, ccu_rep::ccu_buf** buffer);
    std::unordered_map<ccu_buffer_handle, ccu_rep::ccu_buf> ccu_buffer_map_{};

    CcuResult get_address_by_handle(ccu_address_handle addr_handle, ccu_rep::address** address);
    std::unordered_map<ccu_address_handle, ccu_rep::address> ccu_addr_map_{};

    CcuResult get_local_addr_by_handle(ccu_local_addr_handle handle, ccu_rep::local_addr** local_addr);
    std::unordered_map<ccu_local_addr_handle, ccu_rep::local_addr> ccu_local_addr_map_{};

    CcuResult get_remote_addr_by_handle(ccu_remote_addr_handle handle, ccu_rep::remote_addr** remote_addr);
    std::unordered_map<ccu_remote_addr_handle, ccu_rep::remote_addr> ccu_remote_addr_map_{};

    std::unordered_set<uint32_t> load_arg_used_set_{};

protected:
    // 使用channel中的Variable
    HcclResult create_variable(const ChannelHandle channel, uint32_t var_index, ccu_rep::variable* var);
    ccu_rep::variable create_variable();
    ccu_rep::variable create_expect_var();
    ccu_rep::variable create_continuous_variable();
    ccu_rep::local_addr create_local_addr();
    ccu_rep::remote_addr create_remote_addr();
    ccu_rep::remote_addr get_remote_addr(const ChannelHandle channel, const uint32_t index);
    ccu_rep::local_notify create_local_notify();
    ccu_rep::completed_event create_completed_event();
    ccu_rep::ccu_buf create_ccu_buf();
    ccu_rep::executor create_executor();

    HcclResult create_block_ccu_buf(const uint32_t count, ccu_rep::ccu_buf* ccu_bufs);
    HcclResult create_block_executor(const uint32_t count, ccu_rep::executor* ccu_exes);
    HcclResult create_block_completed_event(const uint32_t count, ccu_rep::completed_event* ccu_events);

    // 内部 *Nb / RecordEvent / WaitEvent 系列：mask 由调用方独立传入，
    // 不再从 CompletedEvent 上读取。
    HcclResult record_event(ccu_rep::completed_event event, uint32_t mask);
    HcclResult wait_event(ccu_rep::completed_event event, uint32_t mask);

    // 数据操作
    HcclResult write_nb(
        const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::local_addr& loc,
        const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask);
    HcclResult write_nb(
        const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::ccu_buf& loc,
        const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask);

    HcclResult read_nb(
        const ChannelHandle channel, const ccu_rep::local_addr& loc, const ccu_rep::remote_addr& rem,
        const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask);
    HcclResult read_nb(
        const ChannelHandle channel, const ccu_rep::ccu_buf& loc, const ccu_rep::remote_addr& rem,
        const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask);

    HcclResult write_reduce_nb(
        const ChannelHandle channel, const ccu_rep::remote_addr& rem, const ccu_rep::local_addr& loc,
        const ccu_rep::variable& len, HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event,
        uint32_t mask);
    HcclResult read_reduce_nb(
        const ChannelHandle channel, const ccu_rep::local_addr& loc, const ccu_rep::remote_addr& rem,
        const ccu_rep::variable& len, HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event,
        uint32_t mask);

    HcclResult local_copy_nb(
        const ccu_rep::local_addr& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
        ccu_rep::completed_event event, uint32_t mask); // dst和src是否都是local
    HcclResult local_copy_nb(
        const ccu_rep::ccu_buf& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
        ccu_rep::completed_event event, uint32_t mask);
    HcclResult local_copy_nb(
        const ccu_rep::local_addr& dst, const ccu_rep::ccu_buf& src, const ccu_rep::variable& len,
        ccu_rep::completed_event event, uint32_t mask);

    HcclResult local_reduce_nb(
        const ccu_rep::local_addr& dst, const ccu_rep::local_addr& src, const ccu_rep::variable& len,
        HcclDataType data_type, HcclReduceOp op_type, ccu_rep::completed_event event, uint32_t mask);
    HcclResult local_reduce_nb(
        const ccu_rep::ccu_buf* bufs, uint32_t count, HcclDataType data_type, HcclDataType output_data_type,
        HcclReduceOp op_type, const ccu_rep::variable& len, ccu_rep::completed_event event, uint32_t mask);

    // 参数操作
    void load(const ccu_rep::variable& var);

    // Variable src中存放内存地址，从地址中加载数据到Variable var中
    void load_variable(const ccu_rep::variable& src, const ccu_rep::variable& var);

    void store_variable(const ccu_rep::variable& var, uint64_t addr);
    // 控制逻辑
    // 宏定义IF、WHILE
    ccu_rep::func_call func(const std::string& label);
    ccu_rep::func_call func(const ccu_rep::variable& func_addr);
    ccu_rep::loop_call loop(const std::string& label);

private:
    ccu_rep::address create_address();
    ccu_rep::local_addr create_local_addr(const ccu_rep::variable& token);

protected:
    group_op_config mo_config_{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};

private:
    template <typename t>
    t create_res_assist(std::array<std::vector<t>, ccu_max_iodie_num>& res_record);
    template <typename t>
    std::vector<t> create_block_res_assist(
        const uint32_t count, std::array<std::vector<t>, ccu_max_iodie_num>& res_record);

private:
    ccu_rep_resource res_{};
    asc_ccu_res_repository res_repo_{};

    std::unordered_set<ChannelHandle> channels_{};

    ccu_rep::ccu_instr_info instr_info_{};
    bool has_instruction_resource_{false};
    HcommCcuResRangePod instruction_resource_{};

    uint32_t load_arg_index_{0};

    uint32_t ccu_version_{HCOMM_CCU_VERSION_INVALID};

    ccu_shared_resource exported_res_{};
    ccu_shared_resource imported_res_{};
    std::vector<group_info> group_op_size_info_;
    std::vector<ccu_profiling_info> all_ccu_profiling_infos_;

    // 记录每个kernel所需常量，适用于A6场景
    std::unordered_map<uint64_t, ccu_rep::variable> const_value2_var_map_;

    struct loop_descriptor {
        std::string label;
        std::shared_ptr<ccu_rep::ccu_rep_loop_block> rep_loop_block;
        std::shared_ptr<ccu_rep::ccu_rep_block> prev_active_block;
        bool body_defined{false};
    };

    struct version_v2_loop_record {
        ccu_rep::variable iter_num_var;
        ccu_rep::variable addr_offset_var;
        ccu_rep::variable ctx_id_var;
    };

    struct loop_group_descriptor {
        ccu_loop_group_config config;
        uint64_t total_loop_num{0};
        uint32_t loop_count{0};
        ccu_rep::variable parallel_var;
        ccu_rep::variable offset_var;
        ccu_rep::variable xn_offset_var;
        std::shared_ptr<ccu_rep::ccu_rep_base> bundle_rep;
        bool is_var_based{false};
        bool is_version_v2{false};
        std::vector<version_v2_loop_record> version_v2_loops;
    };

    struct func_descriptor {
        const void* func_ptr{nullptr};
        std::string label;
        std::shared_ptr<ccu_rep::ccu_rep_func_block> rep_func_block;
        std::shared_ptr<ccu_rep::ccu_rep_block> prev_active_block;
        bool body_defined{false};
    };

    std::unordered_map<ccu_loop, loop_descriptor> loop_map_;
    std::unordered_map<ccu_loop_group, loop_group_descriptor> loop_group_map_;
    CcuResult lookup_loop_group_and_loop(
        ccu_loop_group group, ccu_loop loop, const char* fn_name, const char* create_fn_name,
        loop_group_descriptor*& grp_desc, loop_descriptor*& loop_desc, uint32_t& loop_idx);
    uint32_t loop_handle_counter_{0};
    uint32_t loop_group_handle_counter_{0};
    uint32_t loop_body_depth_{0};

    std::unordered_map<uint64_t, func_descriptor> func_map_;
    std::unordered_map<const void*, uint64_t> func_instance_map_;
    uint64_t func_handle_counter_{0};
    bool in_func_body_{false};
    // loop/func body 内首个非法错误的粘性闩，退出 body 时上抛（void body 无法直接回传）。
    CcuResult body_error_{CcuResult::CCU_SUCCESS};

    std::unordered_map<ccu_loop_executors, std::vector<ccu_rep::executor>> loop_engine_pools_;
    uint32_t loop_engine_pool_counter_{0};

    std::string name_{};
};

} // namespace asc

#endif // ASCCOMM_CCU_KERNEL_H
