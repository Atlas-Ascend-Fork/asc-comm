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

CcuRepRemPostVar::CcuRepRemPostVar(Variable param, const ChannelHandle channel, uint16_t paramIndex,
                                   uint16_t semIndex, uint16_t mask)
    : param(param), channel(channel), paramIndex(paramIndex), semIndex(semIndex), mask(mask)
{
    type       = CcuRepType::REM_POST_VAR;
    instrCount = 1;
}

bool CcuRepRemPostVar::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
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
    CHK_PRT_THROW(channelImpl->GetRmtXnByIndex(paramIndex, rmtXnId) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepRemPostSem][%s] failed to get remote xn id, channelHandle[0x%llx].",
            __func__, channel),
        Hccl::InternalException, "failed to get remote xn id.");

    CHK_PRT_THROW(channelImpl->GetRmtCkeByIndex(semIndex, rmtCkeId) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepRemPostSem][%s] failed to get remote cke id, channelHandle[0x%llx].",
            __func__, channel),
        Hccl::InternalException, "failed to get remote cke id.");

    SyncXnInstr(instr++, rmtXnId, param.Id(), channelImpl->GetChannelId(),
                rmtCkeId, mask, 0, 0, 0, 0, 1);
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
                        HCCL_ERROR("[CcuRepRemPostVar::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
                          Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemPostVar::Describe()
{
    return Hccl::StringFormat("Post Variable[%u] To ParamIndex[%u], Use semIndex[%u] and mask[%04x]", param.Id(), paramIndex,
                        semIndex, mask);
}

}; // namespace CcuRep
}; // namespace hcomm