/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_CCU_PROFILING_H
#define ASC_COMM_CCU_PROFILING_H

#include <cstdint>
#include <vector>

#include "ccu/hcomm/ccu_launch.h"

namespace asc::ccu_profiling {

enum class detail_type : uint8_t {
    task = 0,
    wait_signal = 1,
    group = 2,
};

struct profiling_detail {
    static constexpr uint32_t max_name_length = 128;
    static constexpr uint32_t max_channel_num = 16;

    detail_type type;
    char name[max_name_length];
    uint8_t die_id;
    uint8_t mission_id;
    uint16_t instr_id;
    uint8_t reduce_op_type;
    uint8_t input_data_type;
    uint8_t output_data_type;
    uint64_t data_size;
    uint32_t cke_id;
    uint32_t mask;
    uint16_t channel_id[max_channel_num];
    uint32_t remote_rank_id[max_channel_num];
};

struct ccu_prof_report_context {
    const void* kernel_func = nullptr;
    static constexpr uint32_t max_kernel_name_length = 128;

    char kernel_name[max_kernel_name_length] = {};
    uint32_t die_id = 0;
    uint64_t begin_time = 0;
    uint64_t end_time = 0;
    bool task_info_valid = false;
    uint32_t task_id = 0;
    uint32_t stream_id = 0;
};

bool init_ccu_profiling_once();
bool is_ccu_prof_report_ready();
uint64_t msprof_sys_cycle_time();

bool query_ccu_task_id_and_stream_id(uint32_t& task_id, uint32_t& stream_id);

CcuResult get_ccu_profiling_details(
    ccu_kernel_handle kernel_handle, const void* task_args, uint32_t arg_num, std::vector<profiling_detail>& details);

bool report_ccu_node_api_and_basic_info(const ccu_prof_report_context& ctx, uint32_t thread_id);
bool report_ccu_hccl_task_api_and_info(const ccu_prof_report_context& ctx, uint32_t thread_id);
bool report_ccu_task_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id);
bool report_ccu_wait_signal_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id);
bool report_ccu_group_info(
    const ccu_prof_report_context& ctx, const profiling_detail& detail, uint32_t stream_id, uint32_t task_id,
    uint32_t thread_id);

void report_ccu_data_plane_after_launch(
    const ccu_prof_report_context& ctx, const std::vector<profiling_detail>& details);

} // namespace asc::ccu_profiling

#endif // ASC_COMM_CCU_PROFILING_H
