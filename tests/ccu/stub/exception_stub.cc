/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>
#include <cstdarg>
#include <cstring>
#include "exception_defination.h"

// securec 桩
extern "C" int snprintf_s(char *strDest, size_t destMax, size_t count, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(strDest, destMax, format, args);
    va_end(args);
    return ret;
}

extern "C" int memset_s(void *dest, size_t destMax, int value, size_t count)
{
    if (dest == nullptr || count > destMax) {
        return -1;
    }
    (void)memset(dest, value, count);
    return 0;
}

extern "C" int memcpy_s(void *dest, size_t destMax, const void *src, size_t count)
{
    if (dest == nullptr || src == nullptr || count > destMax) {
        return -1;
    }
    (void)memcpy(dest, src, count);
    return 0;
}

// hcomm logging 桩
bool HcclCheckLogLevel(int logType, int moduleId) { return false; }
bool IsErrorToWarn() { return false; }

namespace Hccl {
std::string ExceptionInfo::GetErrorMsg(const ExceptionType &type)
{
    return "";
}

HcclResult ExceptionInfo::GetErrorCode(const ExceptionType &type)
{
    return static_cast<HcclResult>(0);
}

std::map<ExceptionType, std::string> ExceptionInfo::errorMsgMap;
std::map<ExceptionType, HcclResult> ExceptionInfo::errorCodeMap;
} // namespace Hccl
