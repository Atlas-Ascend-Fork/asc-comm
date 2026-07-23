/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation base header file
 * Create: 2025-02-18
 */

#ifndef HCOMM_CCU_REPRESENTATION_LOADARG_H
#define HCOMM_CCU_REPRESENTATION_LOADARG_H

#include "ccu_rep_base_v1.h"
#include "ccu_datatype_v1.h"

namespace hcomm {
namespace CcuRep {

class CcuRepLoadArg : public CcuRepBase {
public:
    CcuRepLoadArg(const Variable &var, uint16_t argId, uint16_t fullArgId);
    bool        Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep) override;
    std::string Describe() override;
    uint16_t    GetVarId() const;
    uint16_t    GetFullArgId() const;

private:
    Variable var;
    uint16_t argId{0};
    uint16_t fullArgId{0};
};

}; // namespace CcuRep
}; // namespace hcomm
#endif // HCCL_CCU_REPRESENTATION_LOADARG_H