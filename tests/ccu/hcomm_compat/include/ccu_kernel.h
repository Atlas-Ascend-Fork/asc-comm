/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_CCU_KERNEL_H
#define ASCCOMM_CCU_UT_CCU_KERNEL_H

#include "ccu_rep_context_v1.h"
#include "ccu_datatype_v1.h"

namespace hcomm {
class CcuKernel : public CcuRep::CcuRepContext {
public:
    ~CcuKernel() override;
    void Append(std::shared_ptr<CcuRep::CcuRepBase> rep) override;
    CcuRep::Variable CreateVariable();
};
} // namespace hcomm

#endif
