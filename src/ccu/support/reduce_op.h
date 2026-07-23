/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_REDUCE_OP_H
#define ASCCOMM_CCU_REDUCE_OP_H

#include <cstdint>
#include <string>

namespace Hccl {
class ReduceOp {
public:
    enum Value : uint32_t { SUM_VALUE = 0, PROD_VALUE, MAX_VALUE, MIN_VALUE, EQUAL_VALUE, INVALID_VALUE = 255 };
    constexpr ReduceOp(Value value = INVALID_VALUE) : value_(value) {}
    constexpr explicit operator uint32_t() const { return value_; }
    constexpr bool operator==(ReduceOp other) const { return value_ == other.value_; }
    constexpr bool operator!=(ReduceOp other) const { return !(*this == other); }
    constexpr bool operator<(ReduceOp other) const { return value_ < other.value_; }
    std::string Describe() const { return std::to_string(value_); }

    static const ReduceOp SUM, PROD, MAX, MIN, EQUAL, INVALID;
private:
    Value value_;
};

inline const ReduceOp ReduceOp::SUM{ReduceOp::SUM_VALUE};
inline const ReduceOp ReduceOp::PROD{ReduceOp::PROD_VALUE};
inline const ReduceOp ReduceOp::MAX{ReduceOp::MAX_VALUE};
inline const ReduceOp ReduceOp::MIN{ReduceOp::MIN_VALUE};
inline const ReduceOp ReduceOp::EQUAL{ReduceOp::EQUAL_VALUE};
inline const ReduceOp ReduceOp::INVALID{ReduceOp::INVALID_VALUE};
} // namespace Hccl

#endif
