/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 * Description: ccu representation loopgroup bundle header file
 * Create: 2026-03-22
 */

#ifndef HCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H
#define HCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H

#include <vector>
#include "ccu_types.h"
#include "ccu_datatype_v1.h"
#include "ccu_rep_base_v1.h"
#include "ccu_rep_loopblock_v1.h"

namespace hcomm {
namespace CcuRep {

class CcuRepLoopGroupBundle : public CcuRepBase {
public:
    struct LoopEntry {
        CcuLoopConfig config;
        Executor executor;
        std::shared_ptr<CcuRepLoopBlock> repLoopBlock;
        Variable loopParamVar;
        bool isVarBased{false};
    };

    CcuRepLoopGroupBundle(const CcuLoopGroupConfig &config,
                          const Variable &parallelVar, const Variable &offsetVar);
    CcuRepLoopGroupBundle(const Variable &parallelVar, const Variable &offsetVar);

    void AddLoop(const LoopEntry &entry);
    void SetRepeatLoopIdx(uint64_t idx) { repeatLoopIdx_ = idx; }
    void SetTotalLoopNum(uint64_t num) { totalLoopNum_ = num; }

    bool        Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep) override;
    uint16_t    InstrCount() override;
    std::string Describe() override;

    uint16_t GetStartLoopInstrId() const;
    const Variable &GetOffsetParam() const { return offsetVar_; }

private:
    uint16_t LoopGroupInstrOffsetInBundle() const;

    CcuLoopGroupConfig config_;
    Variable parallelVar_;
    Variable offsetVar_;
    uint64_t repeatLoopIdx_{0};
    uint64_t totalLoopNum_{0};
    std::vector<LoopEntry> loops_;
    bool isGroupVarBased_{false};
};

}; // namespace CcuRep
}; // namespace hcomm
#endif // HCOMM_CCU_REPRESENTATION_LOOPGROUP_BUNDLE_H
