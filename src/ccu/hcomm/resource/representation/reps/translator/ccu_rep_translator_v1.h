/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_REP_TRANSLATOR_H
#define HCOMM_CCU_REP_TRANSLATOR_H

#include <memory>
#include <vector>
#include <functional>

#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/resource/microcode/ccu_instr_info_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asccomm {
namespace CcuRep {

constexpr uint32_t CCU_TRANSLATOR_MISSION_SLOT_NUM = 16;
constexpr int CCU_TRANSLATOR_XN_NUM = 4;
constexpr int CCU_TRANSLATOR_GSA_NUM = 3;
constexpr int CCU_TRANSLATOR_CKE_NUM = 2;

class CcuRepTranslator {
public:
    CcuRepTranslator(std::shared_ptr<CcuRepReferenceManager> refManager, const TransDep& transDep);
    static void SetCcuVersion(uint32_t version);
    static uint32_t GetInstrNum();
    void GetRes(CcuRepResource& res);
    CcuInstrInfo Translate(
        CcuKernel* ccuKernel, const std::vector<std::shared_ptr<CcuRepBase>>& repVec, uint16_t startInstrId,
        bool isFuncBlock = false);
    void Translate(
        CcuKernel* ccuKernel, const std::vector<std::shared_ptr<CcuRepBase>>& repVec, CcuInstr*& instr,
        uint16_t& instrId, std::function<bool(std::shared_ptr<CcuRepBase>)> filter);
    void DumpInstruction(const CcuInstrInfo& instrInfo) const;
    void SetTransDep(TransDep transDepIn) { transDep = transDepIn; }
    TransDep& GetTransDep() { return transDep; }

private:
    template <typename T1, typename T2>
    void BuildReference(const std::shared_ptr<CcuRepBase>& rep);
    void PreProcess(std::shared_ptr<CcuRepBase> rep);
    void CommonProcess(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId);
    void FinishMainBlock(CcuInstr*& instr, uint16_t& instrId);
    void DumpRep(const std::vector<std::shared_ptr<CcuRepBase>>& repVec, const CcuInstrInfo& instrInfo) const;
    void BindResource(bool isFuncBlock);

private:
    static const int XN_NUM = CCU_TRANSLATOR_XN_NUM;
    static const int GSA_NUM = CCU_TRANSLATOR_GSA_NUM;
    static const int CKE_NUM = CCU_TRANSLATOR_CKE_NUM;
    std::shared_ptr<CcuRepReferenceManager> refManager{nullptr};
    Variable var[XN_NUM];
    Address addr[GSA_NUM];
    CompletedEvent signal[CKE_NUM]; // Uses discrete CKE resources.
    TransDep transDep{0};
    static uint32_t ccuVersion;
};
}; // namespace CcuRep
}; // namespace asccomm

#endif // HCCL_CCU_REP_TRANSLATOR_H
