/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_CONTEXT_RESOURCE_H
#define ASCCOMM_CCU_CONTEXT_RESOURCE_H

#include <vector>
#include <array>
#include <unordered_map>

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {

struct ccu_rep_resource {
    std::array<std::vector<ccu_rep::ccu_buf>, ccu_max_iodie_num> ccubufs;
    std::array<std::vector<ccu_rep::ccu_buf>, ccu_max_iodie_num> block_ccubufs;
    std::array<std::vector<ccu_rep::executor>, ccu_max_iodie_num> executor;
    std::array<std::vector<ccu_rep::executor>, ccu_max_iodie_num> block_executor;
    std::array<std::vector<ccu_rep::completed_event>, ccu_max_iodie_num> completed_event;
    std::array<std::vector<ccu_rep::completed_event>, ccu_max_iodie_num> block_completed_event;
    std::array<std::vector<ccu_rep::address>, ccu_max_iodie_num> address;
    std::array<std::vector<ccu_rep::address>, ccu_max_iodie_num> block_address;
    std::array<std::vector<ccu_rep::variable>, ccu_max_iodie_num> continuous_variable;
    std::array<std::vector<ccu_rep::variable>, ccu_max_iodie_num> variable;
    std::array<std::vector<ccu_rep::local_notify>, ccu_max_iodie_num> local_notify;
};

// Context共享资源
struct ccu_shared_resource {
    std::unordered_map<std::string, ccu_rep::local_notify> shared_notifies;
};

}; // namespace asc

#endif // ASCCOMM_CCU_CONTEXT_RESOURCE_H
