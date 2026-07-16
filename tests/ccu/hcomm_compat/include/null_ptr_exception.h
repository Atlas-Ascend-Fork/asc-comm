/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_NULL_PTR_EXCEPTION_H
#define ASCCOMM_CCU_UT_NULL_PTR_EXCEPTION_H

#include "hccl_exception.h"

namespace Hccl {
class NullPtrException : public HcclException {
public:
    explicit NullPtrException(const std::string &message) : HcclException(message) {}
};
} // namespace Hccl

#endif
