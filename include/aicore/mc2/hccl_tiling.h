/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/

/*!
* \file hccl_tiling.h
* \brief
*/
#ifndef LIB_HCCL_HCCL_TILING_H
#define LIB_HCCL_HCCL_TILING_H

#include "hccl_tilingdata.h"
#include "kernel_tiling/kernel_tiling.h"
#include "detail/hccl_tiling_impl_def.h"

namespace AscendC {

class Mc2CcTilingConfig {
public:
    explicit Mc2CcTilingConfig(const std::string &groupName, uint32_t opType, const std::string &algConfig,
                               uint32_t reduceType = 0, uint8_t dstDataType = 0, uint8_t srcDataType = 0,
                               uint8_t commEngine = 0);
    virtual ~Mc2CcTilingConfig();

public:
    uint32_t GetTiling(::Mc2InitTiling &tiling);
    uint32_t GetTiling(::Mc2CcTiling &tiling);

public:
    uint32_t SetOpType(uint32_t opType);
    uint32_t SetGroupName(const std::string &groupName);
    uint32_t SetAlgConfig(const std::string &algConfig);
    uint32_t SetReduceType(uint32_t reduceType, uint8_t dstDataType = 0, uint8_t srcDataType = 0);
    uint32_t SetStepSize(uint8_t stepSize);
    uint32_t SetSkipLocalRankCopy(uint8_t skipLocalRankCopy);
    uint32_t SetSkipBufferWindowCopy(uint8_t skipBufferWindowCopy);
    uint32_t SetDebugMode(uint8_t debugMode);
    uint32_t SetQueueNum(uint16_t num);
    uint32_t SetCommBlockNum(uint16_t num);
    uint32_t SetCommEngine(uint8_t commEngine);

private:
    HcclTilingImpl impl_;
};
} // namespace AscendC
#endif

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCCL_TILING_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCCL_TILING_H__
#endif
