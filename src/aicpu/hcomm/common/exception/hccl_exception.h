/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_HCCL_EXCEPTION_H
#define ASCCOMM_HCCL_EXCEPTION_H

#include <exception>
#include <string>
#include "hccl/hccl_types.h"

namespace Asc {

class HcclException : public std::exception {
public:
    HcclException(HcclResult errorCode, const char *prefix, const std::string &message)
        : errorCode_(errorCode), errorMessage_(std::string(prefix) + message) {}

    const char *what() const noexcept override { return errorMessage_.c_str(); }
    HcclResult get_error_code() const { return errorCode_; }

private:
    HcclResult errorCode_;
    std::string errorMessage_;
};

} // namespace Asc

#endif // ASCCOMM_HCCL_EXCEPTION_H
