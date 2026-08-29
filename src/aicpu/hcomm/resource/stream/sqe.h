/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCLV2_SQE_H
#define HCCLV2_SQE_H

#include "hccl/base.h"

namespace Asc {

constexpr u32 AC_SQE_SIZE                          = 64U;
constexpr u8  RT_STARS_DEFAULT_KERNEL_CREDIT       = 254U;
constexpr u8  RT_STARS_NEVER_TIMEOUT_KERNEL_CREDIT = 255U;

constexpr u32 UINT32_BIT_NUM = 32U;
constexpr u32 MASK_32_BIT    = 0xFFFFFFFFU;
constexpr u32 MASK_17_BIT    = 0x0001FFFFU;

enum RtStarsWriteValueSizeType : uint8_t {
    RT_STARS_WRITE_VALUE_SIZE_TYPE_8BIT,
    RT_STARS_WRITE_VALUE_SIZE_TYPE_16BIT,
    RT_STARS_WRITE_VALUE_SIZE_TYPE_32BIT,
    RT_STARS_WRITE_VALUE_SIZE_TYPE_64BIT,
    RT_STARS_WRITE_VALUE_SIZE_TYPE_128BIT,
    RT_STARS_WRITE_VALUE_SIZE_TYPE_256BIT
};

enum class RtStarsMemcpyAsyncDataType {
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT8   = 0x00,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT16  = 0x10,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT32  = 0x20,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_UINT8  = 0x30,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_UINT16 = 0x40,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_UINT32 = 0x50,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP16   = 0x60,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP32   = 0x70,
    RT_STARS_MEMCPY_ASYNC_DATA_TYPE_BFP16  = 0x80,
    RT_STARS_MEMCPY_ASYNC_OP_RESERVED      = 0xf0
};

enum class RtStarsMemcpyAsyncOperationKind {
    RT_STARS_MEMCPY_ASYNC_OP_KIND_CPY   = 0x00,
    RT_STARS_MEMCPY_ASYNC_OP_KIND_ADD   = 0x01,
    RT_STARS_MEMCPY_ASYNC_OP_KIND_MAX   = 0x02,
    RT_STARS_MEMCPY_ASYNC_OP_KIND_MIN   = 0x03,
    RT_STARS_MEMCPY_ASYNC_OP_KIND_EQUAL = 0x04
};
} // namespace Asc
#endif
