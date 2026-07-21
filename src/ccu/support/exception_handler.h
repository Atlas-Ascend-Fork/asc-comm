/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_EXCEPTION_HANDLER_H
#define ASCCOMM_CCU_EXCEPTION_HANDLER_H
#include "exception_util.h"
namespace hccl {
class ExceptionHandler {
public:
    static HcclResult HandleException(const char *)
    {
        try { throw; }
        catch (const Hccl::HcclException &error) { return error.GetErrorCode(); }
        catch (...) { return HCCL_E_INTERNAL; }
    }
};
} // namespace hccl

#define EXCEPTION_HANDLE_BEGIN try {
#define EXCEPTION_HANDLE_END } catch (...) { return hccl::ExceptionHandler::HandleException(__func__); }
#endif
