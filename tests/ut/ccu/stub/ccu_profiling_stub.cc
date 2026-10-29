/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/dfx/ccu_profiling.h"

namespace asc::ccu_profiling {

bool is_ccu_prof_report_ready() { return false; }

uint64_t msprof_sys_cycle_time() { return 0; }

bool query_ccu_task_id_and_stream_id(uint32_t&, uint32_t&) { return false; }

CcuResult get_ccu_profiling_details(ccu_kernel_handle, const void*, uint32_t, std::vector<profiling_detail>& details)
{
    details.clear();
    return CcuResult::CCU_SUCCESS;
}

void report_ccu_data_plane_after_launch(const ccu_prof_report_context&, const std::vector<profiling_detail>&) {}

} // namespace asc::ccu_profiling
