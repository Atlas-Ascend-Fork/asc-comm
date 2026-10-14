/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXCEPTION_UTIL_H
#define ASCCOMM_EXCEPTION_UTIL_H

#include <string>
#include "asccomm_log.h"
#include "string_util.h"

namespace Asc {

template <typename Exception>
[[noreturn]] inline void throw_exception(const std::string& message)
{
    ASCCOMM_ERROR("%s", message.c_str());
    throw Exception(message);
}

template <typename Exception, typename... Args>
[[noreturn]] inline void throw_exception(const char* format, Args... args)
{
    throw_exception<Exception>(string_format(format, args...));
}

} // namespace Asc

#endif // ASCCOMM_EXCEPTION_UTIL_H
