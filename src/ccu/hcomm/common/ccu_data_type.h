/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_DATA_TYPE_H
#define CCU_DATA_TYPE_H

#include <stdint.h>
#include <string>

namespace hcomm {

typedef int8_t DataType;

#define CCU_DATA_TYPE_INVALID  ((DataType)-1)
#define CCU_DATA_TYPE_INT8     ((DataType)0)
#define CCU_DATA_TYPE_INT16    ((DataType)1)
#define CCU_DATA_TYPE_INT32    ((DataType)2)
#define CCU_DATA_TYPE_FP16     ((DataType)3)
#define CCU_DATA_TYPE_FP32     ((DataType)4)
#define CCU_DATA_TYPE_INT64    ((DataType)5)
#define CCU_DATA_TYPE_UINT64   ((DataType)6)
#define CCU_DATA_TYPE_UINT8    ((DataType)7)
#define CCU_DATA_TYPE_UINT16   ((DataType)8)
#define CCU_DATA_TYPE_UINT32   ((DataType)9)
#define CCU_DATA_TYPE_FP64     ((DataType)10)
#define CCU_DATA_TYPE_BFP16    ((DataType)11)
#define CCU_DATA_TYPE_INT128   ((DataType)12)
#define CCU_DATA_TYPE_HIF8     ((DataType)14)
#define CCU_DATA_TYPE_FP8E4M3  ((DataType)15)
#define CCU_DATA_TYPE_FP8E5M2  ((DataType)16)
#define CCU_DATA_TYPE_FP8E8M0  ((DataType)17)

inline std::string CcuDataTypeDescribe(DataType dataType)
{
    switch (dataType) {
        case CCU_DATA_TYPE_INT8:    return "DataType::INT8";
        case CCU_DATA_TYPE_INT16:   return "DataType::INT16";
        case CCU_DATA_TYPE_INT32:   return "DataType::INT32";
        case CCU_DATA_TYPE_FP16:    return "DataType::FP16";
        case CCU_DATA_TYPE_FP32:    return "DataType::FP32";
        case CCU_DATA_TYPE_INT64:   return "DataType::INT64";
        case CCU_DATA_TYPE_UINT64:  return "DataType::UINT64";
        case CCU_DATA_TYPE_UINT8:   return "DataType::UINT8";
        case CCU_DATA_TYPE_UINT16:  return "DataType::UINT16";
        case CCU_DATA_TYPE_UINT32:  return "DataType::UINT32";
        case CCU_DATA_TYPE_FP64:    return "DataType::FP64";
        case CCU_DATA_TYPE_BFP16:   return "DataType::BFP16";
        case CCU_DATA_TYPE_INT128:  return "DataType::INT128";
        case CCU_DATA_TYPE_HIF8:    return "DataType::HIF8";
        case CCU_DATA_TYPE_FP8E4M3: return "DataType::FP8E4M3";
        case CCU_DATA_TYPE_FP8E5M2: return "DataType::FP8E5M2";
        case CCU_DATA_TYPE_FP8E8M0: return "DataType::FP8E8M0";
        default:                    return "DataType::Invalid";
    }
}

} // namespace hcomm

#endif // CCU_DATA_TYPE_H
