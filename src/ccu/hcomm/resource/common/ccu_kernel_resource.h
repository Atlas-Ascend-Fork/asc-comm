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
    std::array<std::vector<ccu_rep::ccu_buf>, CCU_MAX_IODIE_NUM> ccubufs;
    std::array<std::vector<ccu_rep::ccu_buf>, CCU_MAX_IODIE_NUM> block_ccubufs;
    std::array<std::vector<ccu_rep::executor>, CCU_MAX_IODIE_NUM> executor;
    std::array<std::vector<ccu_rep::executor>, CCU_MAX_IODIE_NUM> block_executor;
    std::array<std::vector<ccu_rep::completed_event>, CCU_MAX_IODIE_NUM> completed_event;
    std::array<std::vector<ccu_rep::completed_event>, CCU_MAX_IODIE_NUM> block_completed_event;
    std::array<std::vector<ccu_rep::address>, CCU_MAX_IODIE_NUM> address;
    std::array<std::vector<ccu_rep::address>, CCU_MAX_IODIE_NUM> block_address;
    std::array<std::vector<ccu_rep::variable>, CCU_MAX_IODIE_NUM> continuous_variable;
    std::array<std::vector<ccu_rep::variable>, CCU_MAX_IODIE_NUM> variable;
    std::array<std::vector<ccu_rep::local_notify>, CCU_MAX_IODIE_NUM> local_notify;
};

// Context共享资源
struct ccu_shared_resource {
    std::unordered_map<std::string, ccu_rep::local_notify> shared_notifies;
};

}; // namespace asc

#endif // ASCCOMM_CCU_CONTEXT_RESOURCE_H
