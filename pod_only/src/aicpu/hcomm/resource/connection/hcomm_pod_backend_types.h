/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_BACKEND_TYPES_H
#define HCOMM_POD_BACKEND_TYPES_H

#include "hccl/base.h"

namespace Hccl {

class PodDataType {
public:
    enum Value : uint8_t {
        INT8,
        INT16,
        INT32,
        FP16,
        FP32,
        INT64,
        UINT64,
        UINT8,
        UINT16,
        UINT32,
        FP64,
        BFP16,
        INT128,
        BF16_SAT,
        HIF8,
        FP8E4M3,
        FP8E5M2,
        FP8E8M0,
        COUNT,
        INVALID
    };

    constexpr PodDataType() = default;
    constexpr PodDataType(Value value) : value_(value) {}
    constexpr operator Value() const { return value_; }

private:
    Value value_{INVALID};
};

class PodReduceOp {
public:
    enum Value : uint8_t { SUM, PROD, MAX, MIN, EQUAL, COUNT, INVALID };

    constexpr PodReduceOp() = default;
    constexpr PodReduceOp(Value value) : value_(value) {}
    constexpr operator Value() const { return value_; }

private:
    Value value_{INVALID};
};

struct PodReduceIn {
    PodDataType dataType;
    PodReduceOp reduceOp;

    constexpr PodReduceIn(PodDataType dataTypeValue, PodReduceOp reduceOpValue)
        : dataType(dataTypeValue), reduceOp(reduceOpValue) {}
};

struct PodSqeConfigLite {
    PodSqeConfigLite() : placeOdr(1), compOrder(1), fence(1) {}
    bool cqeEn{true};
    u8 placeOdr : 2;
    u8 compOrder : 1;
    u8 fence : 1;
};

struct PodConnLiteOperationOut {
    u16 pi{0};
};

} // namespace Hccl

#endif // HCOMM_POD_BACKEND_TYPES_H
