/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"

#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"
#include "ccu_channel.h"

namespace hcomm {
namespace CcuRep {

CcuRepBufWrite::CcuRepBufWrite(const ChannelHandle channel, CcuBuf src, RemoteAddr dst, Variable len, CompletedEvent sem,
                               uint16_t mask)
    : channel(channel), src(src), dst(dst), len(len), sem(sem), mask(mask)
{
    type       = CcuRepType::BUF_WRITE;
    instrCount = 1;
}

bool CcuRepBufWrite::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;
    CcuChannel channelView(channel);
    auto *channelImpl = &channelView;
    channelId = channelImpl->GetChannelId();
    TransLocMSToRmtMemInstr(instr++, dst.addr.Id(), dst.token.Id(), src.Id(), len.Id(), channelImpl->GetChannelId(),
                            sem.Id(), mask, 0, 0, 1, 1);

    instrId += instrCount;

    return translated;
}

std::string CcuRepBufWrite::Describe()
{
    CcuChannel channelView(channel);
    auto *channelImpl = &channelView;
    return Hccl::StringFormat("Write CcuBuf[%u] To Rmt Mem[%u], len[%u], ChannalId[%u], sem[%u], mask[%04x]", src.Id(),
                        dst.addr.Id(), len.Id(), channelImpl->GetChannelId(), sem.Id(), mask);
}

}; // namespace CcuRep
}; // namespace hcomm
