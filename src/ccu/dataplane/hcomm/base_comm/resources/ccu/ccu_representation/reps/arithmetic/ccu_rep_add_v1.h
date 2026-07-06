/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation base header file
 * Create: 2025-02-18
 */

#ifndef HCOMM_CCU_REPRESENTATION_ADD_H
#define HCOMM_CCU_REPRESENTATION_ADD_H

#include "ccu_rep_base_v1.h"
#include "ccu_datatype_v1.h"

namespace hcomm {
namespace CcuRep {

class CcuRepAdd : public CcuRepBase {
public:
    explicit CcuRepAdd(const Address &addrC, const Address &addrA, const Variable &varB);
    explicit CcuRepAdd(const Address &addrC, const Address &addrA, const Address &addrB);
    explicit CcuRepAdd(const Variable &varC, const Variable &varA, const Variable &varB);
    explicit CcuRepAdd(const Address &addrA, const Variable &offset);
    explicit CcuRepAdd(const Variable &varA, const Variable &offset);
    bool        Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep) override;
    std::string Describe() override;

private:
    void SetCommonInfo();
    AddSubType subType{AddSubType::INVALID};

    Address addrA;
    Address addrB;
    Address addrC;

    Variable varA;
    Variable varB;
    Variable varC;
};

}; // namespace CcuRep
}; // namespace hcomm
#endif // HCCL_CCU_REPRESENTATION_ADD_H