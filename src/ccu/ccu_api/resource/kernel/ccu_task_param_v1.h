/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_TASK_PARAM_H
#define ASCCOMM_CCU_TASK_PARAM_H

#include <cstdint>

namespace asc {

constexpr uint32_t ccu_sqe_args_len = 13;

struct ccu_task_param {
    uint8_t die_id;
    uint8_t mission_id;
    uint16_t timeout;
    uint32_t inst_start_id;
    uint32_t inst_cnt;
    uint32_t key;
    uint32_t arg_size;
    uint64_t args[ccu_sqe_args_len];
};

}; // namespace asc

#endif // _CCU_TASK_PARAM_H
