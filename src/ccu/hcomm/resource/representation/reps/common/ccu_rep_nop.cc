/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

    using namespace Hccl;

    CcuRepNop::CcuRepNop(CcuInsGeneratorBase* insGeneratorPtr) : insGeneratorPtr_(insGeneratorPtr)
    {
        type = CcuRepType::NOP;
        instrCount = insGeneratorPtr_->GetInstrCount(type);
    }

    bool CcuRepNop::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        this->instrId = instrId;
        translated = true;

        CHK_RET_THROW(
            hcomm::CcuApiException,
            hcomm::StringFormat("[CcuRepNop][%s] failed to translate repNop for instrId[%u] ", __func__, instrId),
            insGeneratorPtr_->CcuRepNopTranslate(ccuKernel, instr, instrId, this, dep));

        instrId += instrCount;

        return translated;
    }

    std::string CcuRepNop::Describe() { return hcomm::StringFormat("Nop"); }

}; // namespace CcuRep
}; // namespace hcomm
