/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_CCU_DEV_MGR_IMP_H
#define ASCCOMM_CCU_UT_CCU_DEV_MGR_IMP_H

#include <stdint.h>

#include <hccl/hccl_types.h>

namespace hcomm {
class CcuDevMgrImp {
public:
    static HcclResult GetXnBaseAddr(uint32_t devLogicId, uint8_t dieId, uint64_t &xnBaseAddr);
};
} // namespace hcomm

#endif
