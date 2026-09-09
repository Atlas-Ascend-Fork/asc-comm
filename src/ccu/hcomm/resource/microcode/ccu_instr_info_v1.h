/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_INSTR_INFO_H
#define CCU_INSTR_INFO_H

#include <vector>
#include "hcomm/resource/microcode/ccu_microcode_v1.h"

namespace asc {
namespace ccu_rep {

struct ccu_instr_info {
    std::vector<ccu_instr> instr_vec;
    uint16_t start_instr_id{0};
    uint16_t instr_count{0};
    uint16_t mission_start_instr_id{0};
    uint16_t mission_instr_count{0};
};

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_INSTR_INFO_H
