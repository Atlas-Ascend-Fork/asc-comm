/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_CONTEXT_ASSIST_H
#define HCOMM_CCU_CONTEXT_ASSIST_H

#include <string>

#include "hcomm/resource/microcode/ccu_assist_pub.h"
#include "hccl/hccl_types.h"

namespace hcomm {
namespace CcuRep {

// 辅助函数
uint64_t GetToken(uint64_t tokenId, uint64_t tokenValue, uint64_t tokenValid);

// 按 token 寄存器位域规则将 (tokenId, tokenValue, tokenValid) 三元组合成单个 64bit token 字段。
// 与 GetToken 行为完全一致，提供更贴近 C API 调用方语义的命名，避免在 C 适配层重复实现位域逻辑。
uint64_t CcuCombineTokenInfo(uint64_t tokenId, uint64_t tokenValue, uint64_t tokenValid);

uint16_t GetCcuReduceType(HcclReduceOp reduceOp);
uint16_t GetCcuDataType(HcclDataType dataType, HcclReduceOp reduceOp);
uint16_t GetUBReduceType(HcclReduceOp reduceOp);
uint16_t GetUBDataType(HcclDataType dataType);

uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum);
uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum);
uint64_t GetParallelParamV2(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum);
uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset);

}; // namespace CcuRep
}; // namespace hcomm

#endif // HCCL_CCU_CONTEXT_ASSIST_H
