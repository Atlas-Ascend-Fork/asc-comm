/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_res_specs.h"

namespace hcomm {
HcclResult CcuResSpecifications::GetResourceAddr(const uint8_t dieId, uint64_t &resourceAddr) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetMissionNum(const uint8_t dieId, uint32_t &missionNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetMsNum(const uint8_t dieId, uint32_t &msNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetLoopEngineNum(const uint8_t dieId, uint32_t &loopNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetCkeNum(const uint8_t dieId, uint32_t &ckeNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetXnNum(const uint8_t dieId, uint32_t &xnNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetInstructionNum(const uint8_t dieId, uint32_t &instrNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetGsaNum(const uint8_t dieId, uint32_t &gsaNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetChannelNum(const uint8_t dieId, uint32_t &channelNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetJettyNum(const uint8_t dieId, uint32_t &jettyNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetPfeNum(const uint8_t dieId, uint32_t &pfeNum) const { return HCCL_SUCCESS; }
HcclResult CcuResSpecifications::GetWqeBBNum(const uint8_t dieId, uint32_t &wqeBBNum) const { return HCCL_SUCCESS; }
} // namespace hcomm
