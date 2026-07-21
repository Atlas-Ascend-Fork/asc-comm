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

CcuRepBufRead::CcuRepBufRead(const ChannelHandle channel, RemoteAddr src, CcuBuf dst, Variable len, CompletedEvent sem,
                             uint32_t mask)
    : channel(channel), src(src), dst(dst), len(len), sem(sem), mask(mask)
{
    type       = CcuRepType::BUF_READ;
    instrCount = 1;
}

bool CcuRepBufRead::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;
    CcuChannel channelView(channel);
    auto *channelImpl = &channelView;
    channelId = channelImpl->GetChannelId();
    TransRmtMemToLocMSInstr(instr++, dst.Id(), src.addr.Id(), src.token.Id(), len.Id(), channelId,
                            sem.Id(), mask, 0, 0, 1, 1);

    instrId += instrCount;

    return translated;
}

std::string CcuRepBufRead::Describe()
{
    CcuChannel channelView(channel);
    auto *channelImpl = &channelView;
    return Hccl::StringFormat("Read Rmt Mem[%u] To CcuBuf[%u], len[%u], ChannalId[%u], sem[%u], mask[%04x]",
                        src.addr.Id(), dst.Id(), len.Id(), channelImpl->GetChannelId(), sem.Id(), mask);
}

}; // namespace CcuRep
}; // namespace hcomm
