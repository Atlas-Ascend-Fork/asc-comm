/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_CONTEXT_ASSIST_H
#define ASCCOMM_CCU_CONTEXT_ASSIST_H

#include <string>

#include "hcomm/resource/microcode/ccu_assist_pub.h"
// 微码辅助层用到的公共类型统一来自 hcomm_types.h（ABI 边界约束）
#include "hcomm/hcomm_types.h"

namespace asc {
namespace ccu_rep {

// 辅助函数
uint64_t get_token(uint64_t token_id, uint64_t token_value, uint64_t token_valid);

// 按 token 寄存器位域规则将 (tokenId, tokenValue, tokenValid) 三元组合成单个 64bit token 字段。
// 与 GetToken 行为完全一致，提供更贴近 C API 调用方语义的命名，避免在 C 适配层重复实现位域逻辑。
uint64_t ccu_combine_token_info(uint64_t token_id, uint64_t token_value, uint64_t token_valid);

uint16_t get_ccu_reduce_type(HcclReduceOp reduce_op);
uint16_t get_ccu_data_type(HcclDataType data_type, HcclReduceOp reduce_op);
uint16_t get_ub_reduce_type(HcclReduceOp reduce_op);
uint16_t get_ub_data_type(HcclDataType data_type);

uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num);
uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num);
uint64_t get_parallel_param_v2(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num);
uint64_t get_offset_param(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset);

}; // namespace ccu_rep
}; // namespace asc

#endif // HCCL_CCU_CONTEXT_ASSIST_H
