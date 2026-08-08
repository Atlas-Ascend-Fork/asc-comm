/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2025-05-21
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"

#include "string_util.h"

#include "exception_util.h"
#include "ccu_api_exception.h"
#include "hcomm_c_adpt.h"

#include "ccu_urma_channel.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

    CcuRepRemMem::CcuRepRemMem(CcuInsGeneratorBase* insGenPtr, const ChannelHandle channel, RemoteAddr rem)
        : insGenPtr(insGenPtr),
          channel(channel),
          rem(rem)
    {
        type = CcuRepType::REM_MEM;
        instrCount = insGenPtr->GetInstrCount(type);
    }

    bool CcuRepRemMem::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        this->instrId = instrId;
        translated = true;

        instrCount = insGenPtr->GetInstrCount(type);
        insGenPtr->CcuRepRemMemTranslate(ccuKernel, instr, this);
        instrId += instrCount;

        return translated;
    }

    std::string CcuRepRemMem::Describe()
    {
        return Hccl::StringFormat("Get Remote Buffer Addr and TokenInfo By Transport");
    }

}; // namespace CcuRep
}; // namespace hcomm
