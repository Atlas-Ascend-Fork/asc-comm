/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"
#include "hcomm_c_adpt.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_v1.h"
#include "ccu_urma_channel.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

    CcuRepRemPostVar::CcuRepRemPostVar(
        CcuInsGeneratorBase* insGenPtr, Variable param, const ChannelHandle channel, uint16_t paramIndex,
        uint16_t semIndex, uint16_t mask)
        : insGenPtr(insGenPtr),
          param(param),
          channel(channel),
          paramIndex(paramIndex),
          semIndex(semIndex),
          mask(mask)
    {
        type = CcuRepType::REM_POST_VAR;
        instrCount = insGenPtr->GetInstrCount(type);
    }

    bool CcuRepRemPostVar::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        this->instrId = instrId;
        translated = true;

        insGenPtr->CcuRepRemPostVarTranslate(ccuKernel, instr, this);
        CHK_PRT_THROW(
            (instrId > UINT16_MAX - instrCount),
            HCCL_ERROR(
                "[CcuRepRemPostVar::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]",
                instrId, instrCount),
            Hccl::InternalException, "integer overflow");
        instrId += instrCount;

        return translated;
    }

    std::string CcuRepRemPostVar::Describe()
    {
        return Hccl::StringFormat(
            "Post Variable[%u] To ParamIndex[%u], Use semIndex[%u] and mask[%04x]", param.Id(), paramIndex, semIndex,
            mask);
    }

}; // namespace CcuRep
}; // namespace hcomm
