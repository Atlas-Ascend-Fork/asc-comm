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
#include "hcomm_c_adpt.h"

#include "../../../../endpoint_pairs/channels/ccu/ccu_urma_channel.h"

namespace hcomm {
namespace CcuRep {

CcuRepWrite::CcuRepWrite(const ChannelHandle channel, RemoteAddr rem, LocalAddr loc, Variable len, CompletedEvent sem,
                         uint16_t mask)
    : channel(channel), rem(rem), loc(loc), len(len), sem(sem), mask(mask)
{
    type = CcuRepType::WRITE;
    instrCount = 1;
}

CcuRepWrite::CcuRepWrite(const ChannelHandle channel, RemoteAddr rem, LocalAddr loc, Variable len,
                         uint16_t dataType, uint16_t opType, CompletedEvent sem, uint16_t mask)
    : channel(channel), rem(rem), loc(loc), len(len), sem(sem), mask(mask),
      dataType(dataType), opType(opType), reduceFlag(1)
{
    type = CcuRepType::WRITE;
    instrCount = 1;
}

bool CcuRepWrite::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;
    void *channelPtr{nullptr};
    auto ret = HcommChannelGet(channel, &channelPtr);
    if (ret != HcclResult::HCCL_SUCCESS) {
        Hccl::THROW<Hccl::CcuApiException>("failed to get ccu channel, type[%d]", type);
    }

    auto *channelImpl = dynamic_cast<CcuUrmaChannel *>(static_cast<Channel *>(channelPtr));
    if (channelImpl == nullptr) {
        Hccl::THROW<Hccl::CcuApiException>("[%s] failed to cast channel[0x%llx] to CcuUrmaChannel",
            __func__, channel);
    }
    channelId = channelImpl->GetChannelId();

    TransLocMemToRmtMemInstr(instr++, rem.addr.Id(), rem.token.Id(), loc.addr.Id(), loc.token.Id(), len.Id(),
                             channelImpl->GetChannelId(), dataType, opType, sem.Id(), mask, 0, 0, 1, 1, reduceFlag);
    
    instrId += instrCount;

    return translated;
}

std::string CcuRepWrite::Describe()
{
    return Hccl::StringFormat(
        "Write RemoteAddr[%u] to LocalAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
        loc.addr.Id(), rem.addr.Id(), len.Id(), sem.Id(), mask, dataType, opType);
}

}; // namespace CcuRep
}; // namespace hcomm