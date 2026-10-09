/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXCEPTION_H
#define ASCCOMM_EXCEPTION_H

#include <exception>
#include <string>
#include "../utils/asccomm_result.h"

namespace Asc {

class AsccommException : public std::exception {
public:
    AsccommException(AsccommResult errorCode, const char* prefix, const std::string& message)
        : errorCode_(errorCode), errorMessage_(std::string(prefix) + message)
    {}

    const char* what() const noexcept override { return errorMessage_.c_str(); }
    AsccommResult get_error_code() const { return errorCode_; }

private:
    AsccommResult errorCode_;
    std::string errorMessage_;
};

} // namespace Asc

#endif // ASCCOMM_EXCEPTION_H
