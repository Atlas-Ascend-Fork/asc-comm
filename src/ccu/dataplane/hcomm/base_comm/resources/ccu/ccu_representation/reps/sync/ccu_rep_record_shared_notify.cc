/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_rep_v1.h"

#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepRecordSharedNotify::CcuRepRecordSharedNotify(const LocalNotify &notify, uint16_t mask)
    : notify_(notify), mask_(mask)
{
    type       = CcuRepType::RECORD_SHARED_NOTIFY;
    instrCount = 1;
}

bool CcuRepRecordSharedNotify::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

    // 非本die时利用环回访问
    if (notify_.DieId() != dep.dieId) {
        SyncCKEInstr(instr++, notify_.Id(), dep.reserveCkeId, mask_,
            dep.reserveChannalId[1], 0, 0, 0, 0, 1);
    } else {
        SetCKEInstr(instr++, notify_.Id(), mask_, 0, 0, 1);
    }
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
        HCCL_ERROR("[CcuRepRecordSharedNotify::Translate]uint16 integer overflow occurs, "
            "instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
        Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRecordSharedNotify::Describe()
{
    return Hccl::StringFormat("Post, Use semIndex[%u] and mask[%04x]", notify_.Id(), mask_);
}

}; // namespace CcuRep
}; // namespace hcomm