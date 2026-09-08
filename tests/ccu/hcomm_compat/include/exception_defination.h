/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_EXCEPTION_DEFINATION_H
#define ASCCOMM_CCU_UT_EXCEPTION_DEFINATION_H

#include <map>
#include <string>

#include "hcomm/hcomm_types.h"

namespace Hccl {

enum class ExceptionType {
    NULL_PTR_EXCEPTION,
    INVALID_PARAMS_EXCEPTION,
    TIMEOUT_EXCEPTION,
    DRV_API_EXCEPTION,
    NETWORK_API_EXCEPTION,
    RUNTIME_API_EXCEPTION,
    RESOURCES_NOT_EXIST_EXCEPTION,
    NOT_SUPPORT_EXCEPTION,
    INTERNAL_EXCEPTION,
    RMA_CONN_EXCEPTION,
    SOCKET_EXCEPTION,
    RES_UNPACKAGE_EXCEPTION,
    HOST_IP_NOT_FOUND_EXCEPTION,
    TRACE_API_EXCEPTION,
    CCU_API_EXCEPTION,
    SUSPENDING_EXCEPTION,
};

class ExceptionInfo {
public:
    static std::string GetErrorMsg(const ExceptionType& type_);
    static HcclResult GetErrorCode(const ExceptionType& type_);

private:
    static std::map<ExceptionType, std::string> errorMsgMap;
    static std::map<ExceptionType, HcclResult> errorCodeMap;
};

} // namespace Hccl

#endif
