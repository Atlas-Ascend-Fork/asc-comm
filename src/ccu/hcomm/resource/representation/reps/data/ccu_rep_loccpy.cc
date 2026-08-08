/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"


namespace hcomm {
namespace CcuRep {

    CcuRepLocCpy::CcuRepLocCpy(
        CcuInsGeneratorBase* insGenPtr, LocalAddr dst, LocalAddr src, Variable len, CompletedEvent sem, uint16_t mask)
        : insGenPtr(insGenPtr),
          dst(dst),
          src(src),
          len(len),
          sem(sem),
          mask(mask)
    {
        type = CcuRepType::LOCAL_CPY;
        instrCount = insGenPtr->GetInstrCount(type);
        useCcuBuffer = false;
    }

    CcuRepLocCpy::CcuRepLocCpy(
        CcuInsGeneratorBase* insGenPtr, LocalAddr dst, LocalAddr src, Variable len, uint16_t dataType, uint16_t opType,
        CompletedEvent sem, uint16_t mask)
        : insGenPtr(insGenPtr),
          dst(dst),
          src(src),
          len(len),
          sem(sem),
          mask(mask),
          dataType(dataType),
          opType(opType)
    {
        type = CcuRepType::LOCAL_REDUCE;
        instrCount = insGenPtr->GetInstrCount(type);
        reduceFlag = 1;
        // A5和A6都走环回
        useCcuBuffer = false;
    }

    CcuRepLocCpy::CcuRepLocCpy(
        CcuInsGeneratorBase* insGenPtr, LocalAddr dst, LocalAddr src, Variable len, const std::vector<CcuBuf>& bufs,
        CompletedEvent sem, uint16_t mask)
        : insGenPtr(insGenPtr),
          dst(dst),
          src(src),
          len(len),
          bufs(bufs),
          sem(sem),
          mask(mask)
    {
        type = CcuRepType::LOCAL_CPY;
        instrCount = insGenPtr->GetInstrCount(type);
        useCcuBuffer = true;
    }

    void CcuRepLocCpy::ValidateInsGeneratorForLocCpy()
    {
        CcuInsGeneratorV1* tmpPtrV1 = dynamic_cast<CcuInsGeneratorV1*>(insGenPtr);
        if (tmpPtrV1 && useCcuBuffer) {
            // 使用了A6场景的ms中转搬运
            hcomm::THROW<hcomm::CcuApiException>("Cannot translate CcuRepLocCpy for A5 when useCcuBuffer is true!");
        }
    }

    uint16_t CcuRepLocCpy::GetFirstBufId()
    {
        if (bufs.size() == 0) {
            hcomm::THROW<hcomm::CcuApiException>("The length of CcuBuffer is 0!");
        }
        return bufs[0].Id();
    }

    uint16_t CcuRepLocCpy::GetUsedBufNum() { return bufs.size(); }

    bool CcuRepLocCpy::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        ValidateInsGeneratorForLocCpy();

        this->instrId = instrId;
        translated = true;

        insGenPtr->CcuRepLocCpyTranslate(ccuKernel, instr, this, dep);
        instrId += instrCount;

        return translated;
    }

    std::string CcuRepLocCpy::Describe()
    {
        return hcomm::StringFormat(
            "Read LocalAddr[%u] to LocalAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
            src.addr.Id(), dst.addr.Id(), len.Id(), sem.Id(), mask, dataType, opType);
    }

}; // namespace CcuRep
}; // namespace hcomm
