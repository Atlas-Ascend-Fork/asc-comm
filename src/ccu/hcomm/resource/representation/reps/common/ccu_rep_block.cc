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

    CcuRepBlock::CcuRepBlock(CcuInsGeneratorBase* insGenPtr, const std::string& label)
        : insGeneratorPtr_(insGenPtr),
          label(label)
    {
        type = CcuRepType::BLOCK;
        instrCount = 0;
    }

    std::vector<std::shared_ptr<CcuRepBase>>& CcuRepBlock::GetReps() { return repVec; }

    void CcuRepBlock::Append(std::shared_ptr<CcuRepBase> rep) { repVec.push_back(rep); }

    const std::string& CcuRepBlock::GetLabel() const { return label; }

    uint16_t CcuRepBlock::InstrCount()
    {
        instrCount = 0;
        for (const auto& repInBlock : repVec) {
            instrCount += repInBlock->InstrCount();
        }
        return instrCount;
    }

    bool CcuRepBlock::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        this->instrId = instrId;
        translated = true;

        constexpr uint16_t numberTwo = 2; // 暂定repBlock中的rep遍历2次，后续优化
        for (uint16_t i = 0; i < numberTwo; i++) {
            for (const auto& repInBlock : GetReps()) {
                if (!repInBlock->Translated()) {
                    repInBlock->Translate(ccuKernel, instr, instrId, dep);
                }
            }
        }

        return translated;
    }

    std::string CcuRepBlock::Describe() { return hcomm::StringFormat("RepBlock"); }

    std::shared_ptr<CcuRepBase> CcuRepBlock::GetRepByInstrId(uint16_t instrId)
    {
        for (const auto& rep : GetReps()) {
            const uint16_t repCount = rep->InstrCount();
            if (repCount == 0) {
                continue;
            }
            const uint16_t startId = rep->StartInstrId();
            const uint16_t endId = startId + repCount - 1;
            if (instrId >= startId && instrId <= endId) {
                return rep;
            }
        }
        return nullptr;
    }

}; // namespace CcuRep
}; // namespace hcomm
