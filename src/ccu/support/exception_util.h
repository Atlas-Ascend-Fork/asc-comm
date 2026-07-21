/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_EXCEPTION_UTIL_H
#define ASCCOMM_CCU_EXCEPTION_UTIL_H

#include <stdexcept>
#include <string>

#include "log.h"
#include "string_util.h"

namespace Hccl {
class HcclException : public std::runtime_error {
public:
    HcclException(HcclResult result, const std::string &message) : std::runtime_error(message), result_(result) {}
    HcclResult GetErrorCode() const { return result_; }
private:
    HcclResult result_;
};
class CcuApiException : public HcclException {
public:
    explicit CcuApiException(const std::string &message) : HcclException(HCCL_E_INTERNAL, message) {}
};
class InternalException : public HcclException {
public:
    explicit InternalException(const std::string &message) : HcclException(HCCL_E_INTERNAL, message) {}
};
class NullPtrException : public HcclException {
public:
    explicit NullPtrException(const std::string &message) : HcclException(HCCL_E_PTR, message) {}
};

template <typename Exception> void THROW(const std::string &message) { throw Exception(message); }
template <typename Exception, typename... Args> void THROW(const char *format, Args... args)
{
    throw Exception(StringFormat(format, args...));
}
template <typename Pointer> void CHECK_NULLPTR(const Pointer &pointer, const std::string &message)
{
    if (pointer == nullptr) throw NullPtrException(message);
}
} // namespace Hccl

#define CHK_PRT_THROW(expression, logStatement, exceptionType, message) \
    do { if (UNLIKELY(expression)) { logStatement; throw exceptionType(message); } } while (0)

#endif
