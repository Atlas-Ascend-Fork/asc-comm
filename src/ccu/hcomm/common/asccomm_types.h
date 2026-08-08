/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * asc-comm self-contained DataType / ReduceOp definitions, replacing the
 * dependency on hcomm's "types/data_type.h" and "types/reduce_op.h".
 *
 * hcomm declares these with the MAKE_ENUM macro which expands to a *class*
 * (not a plain enum) whose instances expose `.Describe()` and have full
 * comparison operators. The migrated CCU data-plane code relies on:
 *   - `Hccl::DataType::FP32` etc. as named constants,
 *   - `==`, `!=`, `<` comparisons (the latter for use as std::map keys),
 *   - implicit conversion from the inner Value enum,
 *   - `.Describe()` returning a human-readable name,
 *   - `DataType::INVALID` as a sentinel.
 *
 * To preserve all of the above without depending on the MAKE_ENUM macro, the
 * two types are hand-written classes below with the same shape. The ordering
 * of enum constants follows hcomm's data_type.h / reduce_op.h verbatim.
 *
 * Namespace is `hcomm`.
 */
#ifndef ASCCOMM_TYPES_H
#define ASCCOMM_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace hcomm {

class DataType {
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
        __COUNT,
        INVALID,
    };

    DataType() = default;
    constexpr DataType(Value v) : value_(v) {}

    constexpr operator Value() const { return value_; }

    constexpr bool operator==(DataType a) const { return value_ == a.value_; }
    constexpr bool operator!=(DataType a) const { return value_ != a.value_; }
    constexpr bool operator<(DataType a) const { return value_ < a.value_; }
    constexpr bool operator==(Value v) const { return value_ == v; }
    constexpr bool operator!=(Value v) const { return value_ != v; }
    constexpr bool operator<(Value v) const { return value_ < v; }

    std::string Describe() const
    {
        static const std::vector<std::string> names = InitStrs();
        if (static_cast<size_t>(value_) >= names.size()) {
            return std::string("DataType::Invalid");
        }
        return names[static_cast<size_t>(value_)];
    }

private:
    Value value_{INVALID};

    static std::vector<std::string> InitStrs()
    {
        return {
            "DataType::INT8",    "DataType::INT16",   "DataType::INT32",   "DataType::FP16",     "DataType::FP32",
            "DataType::INT64",   "DataType::UINT64",  "DataType::UINT8",   "DataType::UINT16",   "DataType::UINT32",
            "DataType::FP64",    "DataType::BFP16",   "DataType::INT128",  "DataType::BF16_SAT", "DataType::HIF8",
            "DataType::FP8E4M3", "DataType::FP8E5M2", "DataType::FP8E8M0", "DataType::__COUNT",  "DataType::INVALID",
        };
    }
};

class ReduceOp {
public:
    enum Value : uint8_t {
        SUM,
        PROD,
        MAX,
        MIN,
        EQUAL,
        __COUNT,
        INVALID,
    };

    ReduceOp() = default;
    constexpr ReduceOp(Value v) : value_(v) {}

    constexpr operator Value() const { return value_; }

    constexpr bool operator==(ReduceOp a) const { return value_ == a.value_; }
    constexpr bool operator!=(ReduceOp a) const { return value_ != a.value_; }
    constexpr bool operator<(ReduceOp a) const { return value_ < a.value_; }
    constexpr bool operator==(Value v) const { return value_ == v; }
    constexpr bool operator!=(Value v) const { return value_ != v; }
    constexpr bool operator<(Value v) const { return value_ < v; }

    std::string Describe() const
    {
        static const std::vector<std::string> names = InitStrs();
        if (static_cast<size_t>(value_) >= names.size()) {
            return std::string("ReduceOp::Invalid");
        }
        return names[static_cast<size_t>(value_)];
    }

private:
    Value value_{INVALID};

    static std::vector<std::string> InitStrs()
    {
        return {"ReduceOp::SUM",   "ReduceOp::PROD",    "ReduceOp::MAX",    "ReduceOp::MIN",
                "ReduceOp::EQUAL", "ReduceOp::__COUNT", "ReduceOp::INVALID"};
    }
};

/* Free-function Describe overloads as a convenience for generic call sites. */
inline const char* Describe(DataType type)
{
    static thread_local std::string buf;
    buf = type.Describe();
    return buf.c_str();
}

inline const char* Describe(ReduceOp op)
{
    static thread_local std::string buf;
    buf = op.Describe();
    return buf.c_str();
}

} // namespace hcomm

#endif // ASCCOMM_TYPES_H
