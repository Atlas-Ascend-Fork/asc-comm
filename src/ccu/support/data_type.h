/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_DATA_TYPE_H
#define ASCCOMM_CCU_DATA_TYPE_H

#include <cstdint>
#include <string>

namespace Hccl {
class DataType {
public:
    enum Value : uint32_t {
        INT8_VALUE = 0, INT16_VALUE, INT32_VALUE, FP16_VALUE, FP32_VALUE, INT64_VALUE,
        UINT64_VALUE, UINT8_VALUE, UINT16_VALUE, UINT32_VALUE, FP64_VALUE, BFP16_VALUE,
        INT128_VALUE, BF16_SAT_VALUE, HIF8_VALUE, FP8E4M3_VALUE, FP8E5M2_VALUE, FP8E8M0_VALUE,
        INVALID_VALUE = 255
    };
    constexpr DataType(Value value = INVALID_VALUE) : value_(value) {}
    constexpr explicit operator uint32_t() const { return value_; }
    constexpr bool operator==(DataType other) const { return value_ == other.value_; }
    constexpr bool operator!=(DataType other) const { return !(*this == other); }
    constexpr bool operator<(DataType other) const { return value_ < other.value_; }
    std::string Describe() const { return std::to_string(value_); }

    static const DataType INT8, INT16, INT32, FP16, FP32, INT64, UINT64, UINT8, UINT16, UINT32;
    static const DataType FP64, BFP16, INT128, BF16_SAT, HIF8, FP8E4M3, FP8E5M2, FP8E8M0, INVALID;
private:
    Value value_;
};

inline const DataType DataType::INT8{DataType::INT8_VALUE};
inline const DataType DataType::INT16{DataType::INT16_VALUE};
inline const DataType DataType::INT32{DataType::INT32_VALUE};
inline const DataType DataType::FP16{DataType::FP16_VALUE};
inline const DataType DataType::FP32{DataType::FP32_VALUE};
inline const DataType DataType::INT64{DataType::INT64_VALUE};
inline const DataType DataType::UINT64{DataType::UINT64_VALUE};
inline const DataType DataType::UINT8{DataType::UINT8_VALUE};
inline const DataType DataType::UINT16{DataType::UINT16_VALUE};
inline const DataType DataType::UINT32{DataType::UINT32_VALUE};
inline const DataType DataType::FP64{DataType::FP64_VALUE};
inline const DataType DataType::BFP16{DataType::BFP16_VALUE};
inline const DataType DataType::INT128{DataType::INT128_VALUE};
inline const DataType DataType::BF16_SAT{DataType::BF16_SAT_VALUE};
inline const DataType DataType::HIF8{DataType::HIF8_VALUE};
inline const DataType DataType::FP8E4M3{DataType::FP8E4M3_VALUE};
inline const DataType DataType::FP8E5M2{DataType::FP8E5M2_VALUE};
inline const DataType DataType::FP8E8M0{DataType::FP8E8M0_VALUE};
inline const DataType DataType::INVALID{DataType::INVALID_VALUE};
} // namespace Hccl

#endif
