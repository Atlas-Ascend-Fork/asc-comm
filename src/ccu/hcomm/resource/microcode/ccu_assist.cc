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

#include "runtime/rt_external_device.h"

#include "exception_util.h" // todo: 需要统一整改为不抛异常
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

namespace {
// UB 内存访问凭证查询结构。runtime 的 rtUbDevQueryInfo 以 void* 收参、不提供该类型定义，
// 故由调用方按 runtime 约定的布局本地声明；字段顺序与宽度不可改动。
struct AscUbTokenInfo {
    uint64_t va;
    uint64_t size;
    uint32_t tokenId;
    uint32_t tokenValue;
};

// 仅用于异常信息，hccl_types.h 未提供枚举名转字符串能力
std::string DescribeDataType(HcclDataType dataType)
{
    switch (dataType) {
        case HCCL_DATA_TYPE_INT8:    return "INT8";
        case HCCL_DATA_TYPE_INT16:   return "INT16";
        case HCCL_DATA_TYPE_INT32:   return "INT32";
        case HCCL_DATA_TYPE_FP16:    return "FP16";
        case HCCL_DATA_TYPE_FP32:    return "FP32";
        case HCCL_DATA_TYPE_INT64:   return "INT64";
        case HCCL_DATA_TYPE_UINT64:  return "UINT64";
        case HCCL_DATA_TYPE_UINT8:   return "UINT8";
        case HCCL_DATA_TYPE_UINT16:  return "UINT16";
        case HCCL_DATA_TYPE_UINT32:  return "UINT32";
        case HCCL_DATA_TYPE_FP64:    return "FP64";
        case HCCL_DATA_TYPE_BFP16:   return "BFP16";
        case HCCL_DATA_TYPE_INT128:  return "INT128";
        case HCCL_DATA_TYPE_HIF8:    return "HIF8";
        case HCCL_DATA_TYPE_FP8E4M3: return "FP8E4M3";
        case HCCL_DATA_TYPE_FP8E5M2: return "FP8E5M2";
        case HCCL_DATA_TYPE_FP8E8M0: return "FP8E8M0";
        default:                     return "Invalid(" + std::to_string(static_cast<int32_t>(dataType)) + ")";
    }
}

std::string DescribeReduceOp(HcclReduceOp reduceOp)
{
    switch (reduceOp) {
        case HCCL_REDUCE_SUM:  return "SUM";
        case HCCL_REDUCE_PROD: return "PROD";
        case HCCL_REDUCE_MAX:  return "MAX";
        case HCCL_REDUCE_MIN:  return "MIN";
        default:               return "Invalid(" + std::to_string(static_cast<int32_t>(reduceOp)) + ")";
    }
}
} // namespace

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

uint16_t GetCcuReduceType(HcclReduceOp reduceOp)
{
    static std::map<HcclReduceOp, uint16_t> ccuReduceTypeMap = {
        {HCCL_REDUCE_SUM, CCU_REDUCE_SUM},
        {HCCL_REDUCE_MAX, CCU_REDUCE_MAX},
        {HCCL_REDUCE_MIN, CCU_REDUCE_MIN},
    };

    if (ccuReduceTypeMap.find(reduceOp) == ccuReduceTypeMap.end()) {
        Hccl::THROW<Hccl::CcuApiException>("Unsupported ReduceOp[%s] for Ccu", DescribeReduceOp(reduceOp).c_str());
    }

    return ccuReduceTypeMap[reduceOp];
}

uint16_t GetCcuDataType(HcclDataType dataType, HcclReduceOp reduceOp)
{
    static std::map<HcclDataType, uint16_t> ccuSumDataTypeMap = {
        {HCCL_DATA_TYPE_FP32, 0},  {HCCL_DATA_TYPE_FP16, 1},    {HCCL_DATA_TYPE_BFP16, 2},
        {HCCL_DATA_TYPE_HIF8, 3},  {HCCL_DATA_TYPE_FP8E4M3, 4}, {HCCL_DATA_TYPE_FP8E5M2, 5},
        {HCCL_DATA_TYPE_INT8, 6},  {HCCL_DATA_TYPE_UINT8, 7},   {HCCL_DATA_TYPE_INT16, 8},
        {HCCL_DATA_TYPE_INT32, 9},
    };

    static std::map<HcclDataType, uint16_t> ccuMaxMinDataTypeMap = {
        {HCCL_DATA_TYPE_FP32, 0},  {HCCL_DATA_TYPE_FP16, 1},  {HCCL_DATA_TYPE_BFP16, 2}, {HCCL_DATA_TYPE_INT8, 6},
        {HCCL_DATA_TYPE_UINT8, 7}, {HCCL_DATA_TYPE_INT16, 8}, {HCCL_DATA_TYPE_INT32, 9},

    };

    uint16_t ccuReduceType = GetCcuReduceType(reduceOp);
    if (ccuReduceType == CCU_REDUCE_SUM) {
        if (ccuSumDataTypeMap.find(dataType) == ccuSumDataTypeMap.end()) {
            Hccl::THROW<Hccl::CcuApiException>(
                "Unsupported DataType[%s] for Ccu SUM", DescribeDataType(dataType).c_str());
        }
        return ccuSumDataTypeMap[dataType];
    }

    if (ccuReduceType == CCU_REDUCE_MAX || ccuReduceType == CCU_REDUCE_MIN) {
        if (ccuMaxMinDataTypeMap.find(dataType) == ccuMaxMinDataTypeMap.end()) {
            Hccl::THROW<Hccl::CcuApiException>(
                "Unsupported DataType[%s] for Ccu MAX/MIN", DescribeDataType(dataType).c_str());
        }
        return ccuMaxMinDataTypeMap[dataType];
    }

    return ccuSumDataTypeMap[dataType];
}

uint16_t GetUBReduceType(HcclReduceOp reduceOp)
{
    static std::map<HcclReduceOp, uint16_t> ubReduceTypeMap = {
        {HCCL_REDUCE_SUM, 10},
        {HCCL_REDUCE_MAX, 8},
        {HCCL_REDUCE_MIN, 9},
    };

    if (ubReduceTypeMap.find(reduceOp) == ubReduceTypeMap.end()) {
        Hccl::THROW<Hccl::CcuApiException>("Unsupported reduceOp[%s] for UB Reduce", DescribeReduceOp(reduceOp).c_str());
    }

    return ubReduceTypeMap[reduceOp];
}

uint16_t GetUBDataType(HcclDataType dataType)
{
    static std::map<HcclDataType, uint16_t> ubDataTypeMap = {
        {HCCL_DATA_TYPE_FP32, 7},  {HCCL_DATA_TYPE_FP16, 6},   {HCCL_DATA_TYPE_BFP16, 8},
        {HCCL_DATA_TYPE_INT8, 0},  {HCCL_DATA_TYPE_UINT8, 3},  {HCCL_DATA_TYPE_INT16, 1},
        {HCCL_DATA_TYPE_INT32, 2}, {HCCL_DATA_TYPE_UINT16, 4}, {HCCL_DATA_TYPE_UINT32, 5}};

    if (ubDataTypeMap.find(dataType) == ubDataTypeMap.end()) {
        Hccl::THROW<Hccl::CcuApiException>("Unsupported DataType[%s] for UB Reduce", DescribeDataType(dataType).c_str());
    }
    return ubDataTypeMap[dataType];
}

uint64_t GetTokenInfo(uint64_t va, uint64_t size)
{
    // runtime 的 rtUbDevQueryInfo 以 void* 收取查询结构，布局由调用方按约定解释。
    AscUbTokenInfo info{};
    info.va = va;
    info.size = size;
    if (rtUbDevQueryInfo(QUERY_PROCESS_TOKEN, &info) != RT_ERROR_NONE) {
        // token 信息属于安全信息，失败时不打印 va/size 之外的内容
        Hccl::THROW<Hccl::CcuApiException>("failed to query tokenInfo.");
    }
    // runtime 返回的 tokenId 低 8 位非 CCU 所用语义，需右移后再写入 token 寄存器位域
    constexpr uint32_t tokenIdShiftBit = 8;
    info.tokenId >>= tokenIdShiftBit;
    return CcuRep::GetToken(info.tokenId, info.tokenValue, 1);
}

}; // namespace CcuRep
}; // namespace hcomm
