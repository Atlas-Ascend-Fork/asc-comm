/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_UT_CONST_VAL_H
#define ASCCOMM_CCU_UT_CONST_VAL_H

#include <stdint.h>

#include <stdexcept>
#include <string>

#include "null_ptr_exception.h"

namespace Hccl {
constexpr uint32_t INVALID_U32 = UINT32_MAX;

class InternalException : public std::runtime_error {
public:
    explicit InternalException(const std::string &message) : std::runtime_error(message) {}
};

template <typename T>
inline void CHECK_NULLPTR(const T &pointer, const char *message)
{
    if (pointer == nullptr) {
        throw NullPtrException(message);
    }
}
} // namespace Hccl

#endif
