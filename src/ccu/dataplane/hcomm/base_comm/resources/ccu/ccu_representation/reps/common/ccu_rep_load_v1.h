/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation load header file
 * Create: 2025-02-21
 */

#ifndef HCOMM_CCU_REPRESENTATION_LOAD_H
#define HCOMM_CCU_REPRESENTATION_LOAD_H

#include "ccu_rep_base_v1.h"
#include "ccu_datatype_v1.h"

namespace hcomm {
namespace CcuRep {

class CcuRepLoad : public CcuRepBase {
public:
    CcuRepLoad(uint64_t addr, const Variable &var, uint32_t num = 1);
    bool        Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep) override;
    std::string Describe() override;

private:
    Variable var;
    uint64_t addr;
    uint32_t num;
    uint16_t mask{1};
};

}; // namespace CcuRep
}; // namespace hcomm
#endif // HCCL_CCU_REPRESENTATION_LOAD_H