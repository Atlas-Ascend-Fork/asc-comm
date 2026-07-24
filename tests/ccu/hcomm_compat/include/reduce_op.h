/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_REDUCE_OP_H
#define ASCCOMM_CCU_UT_REDUCE_OP_H

#include <map>
#include <string>

namespace Hccl {

class ReduceOp {
public:
    enum Value { SUM, PROD, MAX, MIN, EQUAL, INVALID };

    constexpr ReduceOp(Value value) : value_(value) {}

    bool operator<(const ReduceOp &other) const { return value_ < other.value_; }
    bool operator==(const ReduceOp &other) const { return value_ == other.value_; }

    std::string Describe() const
    {
        static constexpr const char *names[] = {"SUM", "PROD", "MAX", "MIN", "EQUAL", "INVALID"};
        return names[static_cast<unsigned int>(value_)];
    }

private:
    Value value_;
};

} // namespace Hccl

#endif
