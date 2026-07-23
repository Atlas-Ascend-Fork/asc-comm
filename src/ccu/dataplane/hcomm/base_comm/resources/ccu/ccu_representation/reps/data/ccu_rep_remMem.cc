/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2025-05-21
 */

#include "ccu_rep_v1.h"
#include "ccu_assist_v1.h"

#include "string_util.h"

#include "exception_util.h"
#include "ccu_api_exception.h"
#include "ccu_channel.h"

namespace hcomm {
namespace CcuRep {

CcuRepRemMem::CcuRepRemMem(const ChannelHandle channel, RemoteAddr rem)
    : channel(channel), rem(rem)
{
    type = CcuRepType::REM_MEM;
    instrCount = 2;  // 指令数为2个
}

bool CcuRepRemMem::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

    uint64_t addr{0};
    uint32_t size{0}, tokenId{0}, tokenValue{0};
    CcuChannel channelView(channel);
    auto *channelImpl = &channelView;
    CHK_PRT_THROW(channelImpl->GetRmtBuffer(addr, size, tokenId, tokenValue) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[CcuRepRemMem][%s] failed to get remote buffer, channelHandle[0x%llx].",
            __func__, channel),
        Hccl::InternalException, "get rmt buffer failed.");// 当前认为channel只持有一个buffer

    auto tokenInfo = GetToken(tokenId, tokenValue, 1);

    LoadImdToGSAInstr(instr++, rem.addr.Id(), addr);
    LoadImdToXnInstr(instr++, rem.token.Id(), tokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemMem::Describe()
{
    return Hccl::StringFormat("Get Remote Buffer Addr and TokenInfo By Transport");
}

}; // namespace CcuRep
}; // namespace hcomm
