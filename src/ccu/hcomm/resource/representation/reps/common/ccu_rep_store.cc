/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation store implementation file
 * Create: 2025-03-21
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

    using namespace Hccl;

    CcuRepStore::CcuRepStore(CcuInsGeneratorBase* insGenPtr, const Variable& var, uint64_t addr, uint32_t num)
        : insGeneratorPtr_(insGenPtr),
          var(var),
          addr(addr),
          num(num)
    {
        type = CcuRepType::STORE;
        instrCount = insGeneratorPtr_->GetInstrCount(type);
    }

    bool CcuRepStore::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
    {
        this->instrId = instrId;
        translated = true;
        CHK_RET_THROW(
            hcomm::CcuApiException,
            hcomm::StringFormat("[CcuRepStore][%s] failed to translate repStore for instrId[%u] ", __func__, instrId),
            insGeneratorPtr_->CcuRepStoreTranslate(ccuKernel, instr, instrId, this, dep));
        instrId += instrCount;

        return translated;
    }

    std::string CcuRepStore::Describe() { return hcomm::StringFormat("Store([%u], [%llu], [%u])", var.Id(), addr, num); }

}; // namespace CcuRep
}; // namespace hcomm
