/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef MC2_CCU_COLLECTIVE_COMMON_H
#define MC2_CCU_COLLECTIVE_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "graph/arg_desc_info.h"
#include "graph/kernel_launch_info.h"
#include "register/op_impl_registry.h"
#include "tiling/tiling_api.h"

#define ERROR_LOG(...) std::fprintf(stderr, __VA_ARGS__)
// Reused from hccl_api_test: custom_op_ccu/op_host/common.h.
#define MC2_CCU_DTYPES               \
    {ge::DT_INT8,                    \
     ge::DT_INT16,                   \
     ge::DT_INT32,                   \
     ge::DT_FLOAT,                   \
     ge::DT_INT64,                   \
     ge::DT_UINT64,                  \
     ge::DT_UINT8,                   \
     ge::DT_UINT16,                  \
     ge::DT_UINT32,                  \
     ge::DT_FLOAT16,                 \
     ge::DT_BF16,                    \
     ge::DT_DOUBLE,                  \
     ge::DT_FLOAT8_E8M0,             \
     ge::DataType::DT_FLOAT8_E4M3FN, \
     ge::DataType::DT_FLOAT8_E5M2,   \
     ge::DataType::DT_HIFLOAT8}

#define MC2_CCU_SPLIT_DTYPES                                                                                         \
    {ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, \
     ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32}

#define MC2_CCU_FORMATS                                                                        \
    {ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, \
     ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, \
     ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND}

namespace {
constexpr uint32_t MC2_CCU_TILING_KEY = 0;
constexpr uint32_t MC2_CCU_BLOCK_DIM = 1;

enum HcclCMDType {
    HCCL_CMD_ALLREDUCE = 2,
    HCCL_CMD_ALLGATHER = 6,
    HCCL_CMD_REDUCE_SCATTER = 7,
    HCCL_CMD_ALLTOALLV = 8,
    HCCL_CMD_ALLTOALL = 10,
};

// Keep this legacy mapping type distinct from the public SDK's HcclDataType.
enum class CcuBenchDataType : uint8_t {
    INT8 = 0,
    INT16 = 1,
    INT32 = 2,
    FP16 = 3,
    FP32 = 4,
    INT64 = 5,
    UINT64 = 6,
    UINT8 = 7,
    UINT16 = 8,
    UINT32 = 9,
    FP64 = 10,
    BFP16 = 11,
    HIF8 = 14,
    FP8E4M3 = 15,
    FP8E5M2 = 16,
    FP8E8M0 = 17,
};

const std::map<ge::DataType, CcuBenchDataType> HCCL_DATA_TYPE_MAP = {
    {ge::DT_INT8, CcuBenchDataType::INT8},           {ge::DT_INT16, CcuBenchDataType::INT16},
    {ge::DT_INT32, CcuBenchDataType::INT32},         {ge::DT_FLOAT16, CcuBenchDataType::FP16},
    {ge::DT_FLOAT, CcuBenchDataType::FP32},          {ge::DT_INT64, CcuBenchDataType::INT64},
    {ge::DT_UINT64, CcuBenchDataType::UINT64},       {ge::DT_UINT8, CcuBenchDataType::UINT8},
    {ge::DT_UINT16, CcuBenchDataType::UINT16},       {ge::DT_UINT32, CcuBenchDataType::UINT32},
    {ge::DT_DOUBLE, CcuBenchDataType::FP64},         {ge::DT_BF16, CcuBenchDataType::BFP16},
    {ge::DT_HIFLOAT8, CcuBenchDataType::HIF8},       {ge::DT_FLOAT8_E4M3FN, CcuBenchDataType::FP8E4M3},
    {ge::DT_FLOAT8_E5M2, CcuBenchDataType::FP8E5M2}, {ge::DT_FLOAT8_E8M0, CcuBenchDataType::FP8E8M0},
};

ge::graphStatus CheckCollectiveInputs(gert::TilingContext* context, size_t algorithm_index)
{
    if (context == nullptr || context->GetAttrs() == nullptr || context->GetInputShape(0) == nullptr ||
        context->GetInputDesc(0) == nullptr || context->GetOutputDesc(0) == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const auto& shape = context->GetInputShape(0)->GetStorageShape();
    if (shape.GetDimNum() != 2 || shape.GetShapeSize() <= 0 ||
        context->GetInputDesc(0)->GetStorageFormat() != ge::FORMAT_ND ||
        context->GetOutputDesc(0)->GetStorageFormat() != ge::FORMAT_ND ||
        HCCL_DATA_TYPE_MAP.count(context->GetInputDesc(0)->GetDataType()) == 0 ||
        context->GetInputDesc(0)->GetDataType() != context->GetOutputDesc(0)->GetDataType()) {
        return ge::GRAPH_FAILED;
    }
    const auto* attrs = context->GetAttrs();
    const auto* group = attrs->GetAttrPointer<char>(0);
    const auto* algorithm = attrs->GetAttrPointer<char>(algorithm_index);
    const auto* engine = attrs->GetAttrPointer<int64_t>(algorithm_index + 1);
    if (group == nullptr || group[0] == '\0' || algorithm == nullptr || std::string(algorithm).size() > 127 ||
        engine == nullptr || (*engine != 5 && *engine != 6)) {
        return ge::GRAPH_FAILED;
    }
    for (size_t index = 1; index < algorithm_index; ++index) {
        if (attrs->GetAttrPointer<int64_t>(index) == nullptr) {
            return ge::GRAPH_FAILED;
        }
    }
    return ge::GRAPH_SUCCESS;
}

uint64_t GetInputElementCount(gert::TilingContext* context)
{
    return static_cast<uint64_t>(context->GetInputShape(0)->GetStorageShape().GetShapeSize());
}

uint8_t GetHcclDataType(ge::DataType data_type) { return static_cast<uint8_t>(HCCL_DATA_TYPE_MAP.at(data_type)); }

// tiling 侧需要字节数（kernel 的 rowBytes = K * dataTypeSize）。
inline size_t GetDataTypeSizeUsed(ge::DataType data_type)
{
    switch (data_type) {
        case ge::DT_INT8:
        case ge::DT_UINT8:
        case ge::DT_HIFLOAT8:
        case ge::DT_FLOAT8_E4M3FN:
        case ge::DT_FLOAT8_E5M2:
        case ge::DT_FLOAT8_E8M0:
            return 1U;
        case ge::DT_INT16:
        case ge::DT_UINT16:
        case ge::DT_FLOAT16:
        case ge::DT_BF16:
            return 2U;
        case ge::DT_INT32:
        case ge::DT_UINT32:
        case ge::DT_FLOAT:
            return 4U;
        case ge::DT_INT64:
        case ge::DT_UINT64:
        case ge::DT_DOUBLE:
            return 8U;
        default:
            return 0U;
    }
}

ge::graphStatus ApplyRuntimeCcuConfig(
    AscendC::Mc2CcTilingConfig& config, gert::TilingContext* context, size_t algorithm_index)
{
    const auto* attrs = context->GetAttrs();
    const auto* algorithm = attrs->GetAttrPointer<char>(algorithm_index);
    const auto* engine = attrs->GetAttrPointer<int64_t>(algorithm_index + 1);
    // The tiling allocation API uses its own engine domain: both CCU modes
    // allocate with 6. The Host communicator selects MS or SCHED separately.
    if (algorithm == nullptr || engine == nullptr || (*engine != 5 && *engine != 6) || config.SetCommEngine(6U) != 0 ||
        (algorithm[0] != '\0' && config.SetAlgConfig(std::string(algorithm)) != 0)) {
        return ge::GRAPH_FAILED;
    }
    return ge::GRAPH_SUCCESS;
}
} // namespace

namespace ge {
static Status CommonCcuMc2CalcParamFunc(gert::ExeResGenerationContext* context)
{
    if (context == nullptr) {
        return GRAPH_FAILED;
    }
    gert::StreamInfo stream;
    stream.name = "ccu server";
    stream.reuse_key = "ccu_stream";
    stream.required = true;
    std::vector<gert::StreamInfo> stream_infos{stream};
    return context->SetAttachedStreamInfos(stream_infos);
}

static Status CommonCcuMc2GenTask(
    const gert::ExeResGenerationContext* context, std::vector<std::vector<uint8_t>>& tasks)
{
    if (context == nullptr || tasks.empty()) {
        return GRAPH_FAILED;
    }
    auto ccu = KernelLaunchInfo::CreateCcuTask(context, {"group"});
    auto core = KernelLaunchInfo::LoadFromData(context, tasks.back());
    int64_t block_dim = -1;
    if (!context->GetIntAttrVal("tvm_blockdim", block_dim) || block_dim != 1) {
        return GRAPH_FAILED;
    }
    core.SetBlockDim(block_dim);
    const char* format = core.GetArgsFormat();
    if (format == nullptr || context->GetStreamId() < 0) {
        return GRAPH_FAILED;
    }
    auto args = ArgsFormatSerializer::Deserialize(format);
    args.insert(args.begin(), ArgDescInfo::CreateHiddenInput(HiddenInputSubType::kHcom));
    auto fusion = KernelLaunchInfo::CreateFusionTask(context, {ccu, core});
    if (fusion.SetArgsFormat(ArgsFormatSerializer::Serialize(args).GetString()) != GRAPH_SUCCESS) {
        return GRAPH_FAILED;
    }
    fusion.SetStreamId(context->GetStreamId());
    tasks.back() = fusion.Serialize();
    return GRAPH_SUCCESS;
}
} // namespace ge

#endif
