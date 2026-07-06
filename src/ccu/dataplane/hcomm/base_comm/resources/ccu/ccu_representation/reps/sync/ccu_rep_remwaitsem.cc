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

CcuRepRemWaitSem::CcuRepRemWaitSem(const ChannelHandle channel, uint16_t semIndex, uint16_t mask, bool isProfiling)
    : channel(channel), semIndex(semIndex), mask(mask), isProfiling(isProfiling)
{
    type       = CcuRepType::REM_WAIT_SEM;
    instrCount = 1;
}

bool CcuRepRemWaitSem::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
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
    CHK_PRT_THROW(channelImpl->GetLocCkeByIndex(semIndex, signalId) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepRemWaitSem][%s] failed to get to loc cke id.", __func__),
        Hccl::InternalException, "failed to get resource");

    // 需要profiling的使用SetCKEInstr, 否则使用ClearCKEInstr
    if (isProfiling) {
        SetCKEInstr(instr++, 0, 0, signalId, mask, 1);
    } else {
        ClearCKEInstr(instr++, 0, 0, signalId, mask, 1);
    }
    CHK_PRT_THROW(instrId > UINT16_MAX - instrCount,
                        HCCL_ERROR("[CcuRepRemWaitSem::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
                          Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemWaitSem::Describe()
{
    return Hccl::StringFormat("Wait, Use semIndex[%u] and mask[%04x]", semIndex, mask);
}

}; // namespace CcuRep
}; // namespace hcomm