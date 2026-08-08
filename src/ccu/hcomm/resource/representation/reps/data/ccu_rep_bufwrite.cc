/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm_c_adpt.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_v1.h"
#include "hcomm/resource/channel/ccu_channel.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

CcuRepBufWrite::CcuRepBufWrite(
    CcuInsGeneratorBase* insGenPtr, const ChannelHandle channel, CcuBuf src, RemoteAddr dst, Variable len,
    CompletedEvent sem, uint16_t mask)
    : insGenPtr(insGenPtr), channel(channel), src(src), dst(dst), len(len), sem(sem), mask(mask)
{
    type = CcuRepType::BUF_WRITE;
    instrCount = 1;
}

bool CcuRepBufWrite::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    instrCount = insGenPtr->GetInstrCount(type);
    insGenPtr->CcuRepBufWriteTranslate(ccuKernel, instr, this, dep);
    instrId += instrCount;

    return translated;
}

std::string CcuRepBufWrite::Describe()
{
    CcuChannel channelImpl(channel);
    if (channelImpl.GetResult() != HcclResult::HCCL_SUCCESS) {
        hcomm::THROW<hcomm::CcuApiException>("failed to get ccu channel, type[%d]", type);
    }
    return hcomm::StringFormat(
        "Write CcuBuf[%u] To Rmt Mem[%u], len[%u], ChannalId[%u], sem[%u], mask[%04x]", src.Id(), dst.addr.Id(),
        len.Id(), channelImpl->GetChannelId(), sem.Id(), mask);
}

}; // namespace CcuRep
}; // namespace hcomm
