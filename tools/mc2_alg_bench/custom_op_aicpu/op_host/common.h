/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_API_TEST_CUSTOM_OP_COMMON_H
#define HCCL_API_TEST_CUSTOM_OP_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "graph/arg_desc_info.h"
#include "graph/kernel_launch_info.h"
#include "register/op_impl_registry.h"
#include "tiling/tiling_api.h"

#define INFO_LOG(fmt, args...) fprintf(stdout, "[INFO]  " fmt "\n", ##args)
#define ERROR_LOG(fmt, args...) fprintf(stderr, "[ERROR] " fmt "\n", ##args)

#define HCCL_API_TEST_DTYPES         \
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

#define HCCL_API_TEST_SPLIT_DTYPES                                                                                   \
    {ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, \
     ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32, ge::DT_INT32}

#define HCCL_API_TEST_FORMATS                                                                  \
    {ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, \
     ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, \
     ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND, ge::FORMAT_ND}

namespace {

// The perf kernels have a single generic path and are compiled with the default function entry 0.
constexpr uint32_t HCCL_API_TEST_TILING_KEY = 0U;
constexpr uint32_t HCCL_API_TEST_BLOCK_DIM = 1U;
// Mc2CcTilingConfig consumes the HcclAccelerator value; 2 selects AICPU_TS.
constexpr uint32_t HCCL_API_TEST_AICPU_TS_COMM_ENGINE = 2U;
constexpr char HCCL_GROUP_NAME[] = "group";
constexpr char AICPU_SO_NAME[] = "libccl_kernel.so";
constexpr char AICPU_KERNEL_NAME[] = "RunAicpuKfcSrvLaunch";

struct HcclCommParamDesc {
    uint64_t version : 4;
    uint64_t group_num : 4;
    uint64_t has_ffts : 1;
    uint64_t tiling_off : 7;
    uint64_t is_dyn : 48;
};

enum HcclCMDType {
    HCCL_CMD_ALLREDUCE = 2,
    HCCL_CMD_ALLGATHER = 6,
    HCCL_CMD_REDUCE_SCATTER = 7,
    HCCL_CMD_ALLTOALLV = 8,
    HCCL_CMD_ALLTOALL = 10,
};

enum class HcclDataType : uint8_t {
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

const std::map<ge::DataType, HcclDataType> HCCL_DATA_TYPE_MAP = {
    {ge::DT_INT8, HcclDataType::INT8},           {ge::DT_INT16, HcclDataType::INT16},
    {ge::DT_INT32, HcclDataType::INT32},         {ge::DT_FLOAT16, HcclDataType::FP16},
    {ge::DT_FLOAT, HcclDataType::FP32},          {ge::DT_INT64, HcclDataType::INT64},
    {ge::DT_UINT64, HcclDataType::UINT64},       {ge::DT_UINT8, HcclDataType::UINT8},
    {ge::DT_UINT16, HcclDataType::UINT16},       {ge::DT_UINT32, HcclDataType::UINT32},
    {ge::DT_DOUBLE, HcclDataType::FP64},         {ge::DT_BF16, HcclDataType::BFP16},
    {ge::DT_HIFLOAT8, HcclDataType::HIF8},       {ge::DT_FLOAT8_E4M3FN, HcclDataType::FP8E4M3},
    {ge::DT_FLOAT8_E5M2, HcclDataType::FP8E5M2}, {ge::DT_FLOAT8_E8M0, HcclDataType::FP8E8M0},
};

ge::graphStatus CheckPerfTensor(gert::TilingContext* context)
{
    if (context == nullptr || context->GetInputShape(0) == nullptr || context->GetInputDesc(0) == nullptr ||
        context->GetOutputDesc(0) == nullptr) {
        return ge::GRAPH_FAILED;
    }
    const auto& shape = context->GetInputShape(0)->GetStorageShape();
    if (shape.GetDimNum() != 2 || shape.GetShapeSize() <= 0) {
        ERROR_LOG("Input must be a non-empty two-dimensional tensor");
        return ge::GRAPH_FAILED;
    }
    const auto input_format = context->GetInputDesc(0)->GetStorageFormat();
    const auto output_format = context->GetOutputDesc(0)->GetStorageFormat();
    if (input_format != ge::FORMAT_ND || output_format != ge::FORMAT_ND) {
        ERROR_LOG("Only ND format is supported");
        return ge::GRAPH_FAILED;
    }
    if (HCCL_DATA_TYPE_MAP.count(context->GetInputDesc(0)->GetDataType()) == 0 ||
        HCCL_DATA_TYPE_MAP.count(context->GetOutputDesc(0)->GetDataType()) == 0) {
        ERROR_LOG("Unsupported communication data type");
        return ge::GRAPH_FAILED;
    }
    if (context->GetInputDesc(0)->GetDataType() != context->GetOutputDesc(0)->GetDataType()) {
        ERROR_LOG("Input and output data types must be identical");
        return ge::GRAPH_FAILED;
    }
    return ge::GRAPH_SUCCESS;
}

uint64_t GetInputElementCount(gert::TilingContext* context)
{
    return static_cast<uint64_t>(context->GetInputShape(0)->GetStorageShape().GetShapeSize());
}

uint8_t GetHcclDataType(ge::DataType data_type) { return static_cast<uint8_t>(HCCL_DATA_TYPE_MAP.at(data_type)); }

// Empty means SDK automatic selection; a nonempty value uses the public algorithm
// configuration string as supplied by the caller for this individual operation.
ge::graphStatus ApplyRuntimeAlgConfig(
    AscendC::Mc2CcTilingConfig& config, gert::TilingContext* context, size_t algorithm_attr_index)
{
    const char* algorithm = context->GetAttrs()->GetAttrPointer<char>(algorithm_attr_index);
    if (algorithm == nullptr || algorithm[0] == '\0') {
        return ge::GRAPH_SUCCESS;
    }
    if (config.SetAlgConfig(std::string(algorithm)) != 0U) {
        ERROR_LOG("Failed to set runtime algorithm configuration %s", algorithm);
        return ge::GRAPH_FAILED;
    }
    return ge::GRAPH_SUCCESS;
}

} // namespace

namespace ge {

static Status CommonKFCMc2CalcParamFunc(gert::ExeResGenerationContext* context)
{
    if (context == nullptr) {
        return GRAPH_FAILED;
    }
    gert::StreamInfo stream_info;
    stream_info.name = "aicpu kfc server";
    stream_info.reuse_key = "kfc_stream";
    stream_info.depend_value_input_indices = {};
    stream_info.required = true;
    std::vector<gert::StreamInfo> stream_infos{stream_info};
    return context->SetAttachedStreamInfos(stream_infos);
}

static Status CreateAicpuTask(const gert::ExeResGenerationContext* context, KernelLaunchInfo& aicpu_task)
{
    const size_t input_size = context->GetComputeNodeInfo()->GetIrInputsNum();
    const size_t output_size = context->GetComputeNodeInfo()->GetIrOutputsNum();
    union {
        HcclCommParamDesc desc;
        uint64_t value;
    } custom = {};
    custom.desc.version = 1;
    custom.desc.group_num = 1;
    custom.desc.tiling_off = 3UL + input_size + output_size;

    std::vector<ArgDescInfo> args;
    args.emplace_back(ArgDescInfo::CreateCustomValue(custom.value));
    args.emplace_back(ArgDescInfo::CreateHiddenInput(HiddenInputSubType::kHcom));
    for (size_t i = 0; i < input_size; ++i) {
        args.emplace_back(ArgDescInfo(ArgDescType::kIrInput, 0));
    }
    for (size_t i = 0; i < output_size; ++i) {
        args.emplace_back(ArgDescInfo(ArgDescType::kIrOutput, i));
    }
    args.emplace_back(ArgDescInfo(ArgDescType::kWorkspace));
    args.emplace_back(ArgDescInfo(ArgDescType::kTiling));
    return aicpu_task.SetArgsFormat(ArgsFormatSerializer::Serialize(args).GetString());
}

static Status InsertHcomForAicTask(
    const gert::ExeResGenerationContext* context, std::vector<std::vector<uint8_t>>& tasks)
{
    auto task = KernelLaunchInfo::LoadFromData(context, tasks.back());
    const char* format = task.GetArgsFormat();
    if (format == nullptr) {
        return GRAPH_FAILED;
    }
    auto args = ArgsFormatSerializer::Deserialize(format);
    auto position = args.begin();
    while (position != args.end() && position->GetType() != ArgDescType::kIrInput) {
        ++position;
    }
    args.insert(position, ArgDescInfo::CreateHiddenInput(HiddenInputSubType::kHcom));
    if (task.SetArgsFormat(ArgsFormatSerializer::Serialize(args).GetString()) != GRAPH_SUCCESS) {
        return GRAPH_FAILED;
    }
    tasks.back() = task.Serialize();
    return GRAPH_SUCCESS;
}

static Status CommonKFCMc2GenTask(
    const gert::ExeResGenerationContext* context, std::vector<std::vector<uint8_t>>& tasks)
{
    if (context == nullptr || tasks.empty()) {
        return GRAPH_FAILED;
    }
    const auto stream_infos = context->GetAttachedStreamInfos();
    if (stream_infos.empty()) {
        return GRAPH_FAILED;
    }
    const int64_t attached_stream = stream_infos[0].stream_id;

    auto wait_task = KernelLaunchInfo::CreateHcomWaitTask(context, HCCL_GROUP_NAME);
    wait_task.SetStreamId(attached_stream);
    tasks.insert(tasks.begin(), wait_task.Serialize());

    auto aicpu_task = KernelLaunchInfo::CreateAicpuKfcTask(context, AICPU_SO_NAME, AICPU_KERNEL_NAME);
    aicpu_task.SetStreamId(attached_stream);
    if (CreateAicpuTask(context, aicpu_task) != GRAPH_SUCCESS) {
        return GRAPH_FAILED;
    }
    tasks.insert(tasks.begin() + 1, aicpu_task.Serialize());

    auto record_task = KernelLaunchInfo::CreateHcomRecordTask(context, HCCL_GROUP_NAME);
    record_task.SetStreamId(context->GetStreamId());
    tasks.insert(tasks.begin() + 2, record_task.Serialize());
    return InsertHcomForAicTask(context, tasks);
}

} // namespace ge

#endif
