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
#include <string>
#include <vector>

#include "graph/arg_desc_info.h"
#include "graph/kernel_launch_info.h"
#include "register/op_impl_registry.h"
#include "tiling/tiling_api.h"

#define ERROR_LOG(...) std::fprintf(stderr, __VA_ARGS__)
#define MC2_CCU_DTYPES {ge::DT_FLOAT}
#define MC2_CCU_SPLIT_DTYPES {ge::DT_INT32}
#define MC2_CCU_FORMATS {ge::FORMAT_ND}

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
        context->GetInputDesc(0)->GetDataType() != ge::DT_FLOAT ||
        context->GetOutputDesc(0)->GetDataType() != ge::DT_FLOAT) {
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

// HcclDataType::FP32; every registered data input/output has already been checked.
uint8_t GetHcclDataType(ge::DataType) { return 4; }

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
