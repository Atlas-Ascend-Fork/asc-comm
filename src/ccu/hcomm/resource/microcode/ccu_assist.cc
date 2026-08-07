/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode/ccu_assist_v1.h"

#include "hcomm/resource/microcode/ccu_microcode_v1.h"

#include "hcomm/common/ccu_runtime.h"   // AsccommQueryUbToken（经 pkg_inc ABI 兜底）

#include <map>

namespace hcomm {
namespace CcuRep {

constexpr uint64_t SetBits(uint16_t start, uint16_t end)
{
    return ((uint64_t(1) << (end - start + 1)) - uint64_t(1)) << start;
}

constexpr uint64_t SetBits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum = 8;
    constexpr uint16_t ctxIdShiftBit = 45;
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 13;
    constexpr uint16_t loopNumBitNum = 13;
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit) | ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit) |
           ((loopIterNum & SetBits(loopNumBitNum)) << loopNumShiftBit);
}

uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit) |
           ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

uint64_t GetParallelParamV2(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t loopNumBitNum = 10;
    constexpr uint16_t loopNumShiftBit = 0;
    constexpr uint16_t repeatLoopBitNum = 9;
    constexpr uint16_t repeatLoopShiftBit = 10;
    constexpr uint16_t extendBitNum = 9;
    constexpr uint16_t extendShiftBit = 19;
    return ((totalLoopNum & SetBits(loopNumBitNum)) << loopNumShiftBit) |
           ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((repeatNum & SetBits(extendBitNum)) << extendShiftBit);
}

uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset)
{
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 21;
    constexpr uint16_t msBitNum = 11;
    constexpr uint16_t msShiftBit = 10;
    constexpr uint16_t ckeBitNum = 10;
    constexpr uint16_t ckeShiftBit = 0;
    return ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit) | ((msOffset & SetBits(msBitNum)) << msShiftBit) |
           ((ckeOffset & SetBits(ckeBitNum)) << ckeShiftBit);
}

uint64_t GetToken(uint64_t tokenId, uint64_t tokenValue, uint64_t tokenValid)
{
    constexpr uint16_t tokenValidBitNum = 1;
    constexpr uint16_t tokenValidShiftBit = 52;
    constexpr uint16_t tokenIdBitNum = 20;
    constexpr uint16_t tokenIdShiftBit = 32;
    constexpr uint16_t tokenValueBitNum = 32;
    constexpr uint16_t tokenValueShiftBit = 0;
    return ((tokenValid & SetBits(tokenValidBitNum)) << tokenValidShiftBit) |
           ((tokenId & SetBits(tokenIdBitNum)) << tokenIdShiftBit) |
           ((tokenValue & SetBits(tokenValueBitNum)) << tokenValueShiftBit);
}

uint64_t CcuCombineTokenInfo(uint64_t tokenId, uint64_t tokenValue, uint64_t tokenValid)
{
    return GetToken(tokenId, tokenValue, tokenValid);
}

uint16_t GetCcuReduceType(hcomm::ReduceOp reduceOp)
{
    static std::map<hcomm::ReduceOp, uint16_t> ccuReduceTypeMap = {
        {hcomm::ReduceOp::SUM, CCU_REDUCE_SUM},
        {hcomm::ReduceOp::MAX, CCU_REDUCE_MAX},
        {hcomm::ReduceOp::MIN, CCU_REDUCE_MIN},
    };

    if (ccuReduceTypeMap.find(reduceOp) == ccuReduceTypeMap.end()) {
        hcomm::THROW<hcomm::CcuApiException>("Unsupported ReduceOp[%s] for Ccu", reduceOp.Describe().c_str());
    }

    return ccuReduceTypeMap[reduceOp];
}

uint16_t GetCcuDataType(hcomm::DataType dataType, hcomm::ReduceOp reduceOp)
{
    static std::map<hcomm::DataType, uint16_t> ccuSumDataTypeMap = {
        {hcomm::DataType::FP32, 0},  {hcomm::DataType::FP16, 1},    {hcomm::DataType::BFP16, 2},
        {hcomm::DataType::HIF8, 3},  {hcomm::DataType::FP8E4M3, 4}, {hcomm::DataType::FP8E5M2, 5},
        {hcomm::DataType::INT8, 6},  {hcomm::DataType::UINT8, 7},   {hcomm::DataType::INT16, 8},
        {hcomm::DataType::INT32, 9},
    };

    static std::map<hcomm::DataType, uint16_t> ccuMaxMinDataTypeMap = {
        {hcomm::DataType::FP32, 0},  {hcomm::DataType::FP16, 1},  {hcomm::DataType::BFP16, 2}, {hcomm::DataType::INT8, 6},
        {hcomm::DataType::UINT8, 7}, {hcomm::DataType::INT16, 8}, {hcomm::DataType::INT32, 9},

    };

    uint16_t ccuReduceType = GetCcuReduceType(reduceOp);
    if (ccuReduceType == CCU_REDUCE_SUM) {
        if (ccuSumDataTypeMap.find(dataType) == ccuSumDataTypeMap.end()) {
            hcomm::THROW<hcomm::CcuApiException>(
                "Unsupported hcomm::DataType[%s] for Ccu SUM", dataType.Describe().c_str());
        }
        return ccuSumDataTypeMap[dataType];
    }

    if (ccuReduceType == CCU_REDUCE_MAX || ccuReduceType == CCU_REDUCE_MIN) {
        if (ccuMaxMinDataTypeMap.find(dataType) == ccuMaxMinDataTypeMap.end()) {
            hcomm::THROW<hcomm::CcuApiException>(
                "Unsupported hcomm::DataType[%s] for Ccu MAX/MIN", dataType.Describe().c_str());
        }
        return ccuMaxMinDataTypeMap[dataType];
    }

    return ccuSumDataTypeMap[dataType];
}

uint16_t GetUBReduceType(hcomm::ReduceOp reduceOp)
{
    static std::map<hcomm::ReduceOp, uint16_t> ubReduceTypeMap = {
        {hcomm::ReduceOp::SUM, 10},
        {hcomm::ReduceOp::MAX, 8},
        {hcomm::ReduceOp::MIN, 9},
    };

    if (ubReduceTypeMap.find(reduceOp) == ubReduceTypeMap.end()) {
        hcomm::THROW<hcomm::CcuApiException>("Unsupported reduceOp[%s] for UB Reduce", reduceOp.Describe().c_str());
    }

    return ubReduceTypeMap[reduceOp];
}

uint16_t GetUBDataType(hcomm::DataType dataType)
{
    static std::map<hcomm::DataType, uint16_t> ubDataTypeMap = {
        {hcomm::DataType::FP32, 7},  {hcomm::DataType::FP16, 6},   {hcomm::DataType::BFP16, 8},
        {hcomm::DataType::INT8, 0},  {hcomm::DataType::UINT8, 3},  {hcomm::DataType::INT16, 1},
        {hcomm::DataType::INT32, 2}, {hcomm::DataType::UINT16, 4}, {hcomm::DataType::UINT32, 5}};

    if (ubDataTypeMap.find(dataType) == ubDataTypeMap.end()) {
        hcomm::THROW<hcomm::CcuApiException>("Unsupported hcomm::DataType[%s] for UB Reduce", dataType.Describe().c_str());
    }
    return ubDataTypeMap[dataType];
}

uint64_t GetTokenInfo(uint64_t va, uint64_t size)
{
    uint32_t tokenId = 0;
    uint32_t tokenValue = 0;
    CcuResult ret = AsccommQueryUbToken(va, size, tokenId, tokenValue);
    if (ret != HcclResult::HCCL_SUCCESS) {
        hcomm::THROW<hcomm::CcuApiException>("failed to query tokenInfo.");
    }
    return CcuRep::GetToken(tokenId, tokenValue, 1);
}

}; // namespace CcuRep
}; // namespace hcomm
