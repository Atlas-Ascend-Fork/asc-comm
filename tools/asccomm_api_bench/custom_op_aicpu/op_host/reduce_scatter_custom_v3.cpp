/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 */

#include "../op_kernel/reduce_scatter_custom_v3_tiling.h"
#include "register/op_def_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "tiling/tiling_api.h"
#include "common.h"

namespace optiling {

static ge::graphStatus ReduceScatterCustomTilingFunc(gert::TilingContext* context)
{
    if (CheckPerfTensor(context) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    const char* group = context->GetAttrs()->GetAttrPointer<char>(0);
    const int reduce_type = *context->GetAttrs()->GetAttrPointer<int>(1);
    const int rank_size = *context->GetAttrs()->GetAttrPointer<int>(2);
    const uint64_t input_count = GetInputElementCount(context);
    if (group == nullptr || reduce_type < 0 || reduce_type > 3 || rank_size <= 0 ||
        input_count % static_cast<uint64_t>(rank_size) != 0) {
        ERROR_LOG("ReduceScatter requires a valid group/reduceOp and inputCount divisible by rankSize");
        return ge::GRAPH_FAILED;
    }

    const auto input_type = context->GetInputDesc(0)->GetDataType();
    const auto output_type = context->GetOutputDesc(0)->GetDataType();
    context->SetBlockDim(HCCL_API_TEST_BLOCK_DIM);
    context->SetTilingKey(HCCL_API_TEST_TILING_KEY);
    const uint64_t lib_workspace_size =
        platform_ascendc::PlatformAscendC(context->GetPlatformInfo()).GetLibApiWorkSpaceSize();
    context->GetWorkspaceSizes(1)[0] = lib_workspace_size;

    auto* tiling = context->GetTilingData<ReduceScatterCustomV3TilingData>();
    tiling->param.count = input_count / static_cast<uint64_t>(rank_size);
    tiling->param.reduceType = static_cast<uint32_t>(reduce_type);
    tiling->param.dataType = GetHcclDataType(input_type);

    AscendC::Mc2CcTilingConfig config(
        group, HCCL_CMD_REDUCE_SCATTER, "", reduce_type, GetHcclDataType(output_type), GetHcclDataType(input_type));
    config.SetReduceType(reduce_type, GetHcclDataType(output_type), GetHcclDataType(input_type));
    config.SetCommEngine(HCCL_API_TEST_AICPU_TS_COMM_ENGINE);
    if (ApplyRuntimeAlgConfig(config, context, 6) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    config.GetTiling(tiling->mc2InitTiling);
    config.GetTiling(tiling->mc2CcTiling);
    // INFO_LOG("ReduceScatter recvCount=%llu, rankSize=%d, blockDim=1, workingCore=AIC0",
    //     static_cast<unsigned long long>(tiling->param.count), rank_size);
    return ge::GRAPH_SUCCESS;
}

} // namespace optiling

namespace ge {

static graphStatus InferShape(gert::InferShapeContext* context)
{
    const auto* input = context->GetInputShape(0);
    const int rank_size = *context->GetAttrs()->GetAttrPointer<int>(2);
    auto* output = context->GetOutputShape(0);
    output->SetDimNum(0);
    output->AppendDim(input->GetDim(0) < 0 ? input->GetDim(0) : input->GetDim(0) / rank_size);
    output->AppendDim(input->GetDim(1));
    return GRAPH_SUCCESS;
}

static graphStatus InferDataType(gert::InferDataTypeContext* context)
{
    context->SetOutputDataType(0, context->GetInputDataType(0));
    return GRAPH_SUCCESS;
}

} // namespace ge

namespace ops {

class ReduceScatterCustomV3 : public OpDef {
public:
    explicit ReduceScatterCustomV3(const char* name) : OpDef(name)
    {
        this->Input("x1")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Output("y")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Attr("group").AttrType(REQUIRED).String();
        this->Attr("reduceOp").AttrType(REQUIRED).Int();
        this->Attr("rank_size").AttrType(OPTIONAL).Int(8);
        this->Attr("tileM").AttrType(REQUIRED).Int();
        this->Attr("strideCount").AttrType(REQUIRED).Int();
        this->Attr("commTurn").AttrType(OPTIONAL).Int(0);
        this->Attr("algorithm").AttrType(OPTIONAL).String("");
        OpAICoreConfig config;
        config.DynamicCompileStaticFlag(true)
            .DynamicFormatFlag(true)
            .DynamicRankSupportFlag(true)
            .DynamicShapeSupportFlag(true)
            .NeedCheckSupportFlag(false)
            .PrecisionReduceFlag(true)
            .ExtendCfgInfo("aclnnSupport.value", "support_aclnn")
            .ExtendCfgInfo("jitCompile.flag", "static_false")
            .ExtendCfgInfo("multiKernelSupportDynamicGraph.value", "multi_kernel");
        this->AICore().SetTiling(optiling::ReduceScatterCustomTilingFunc);
        this->AICore().AddConfig("ascend950", config);
        this->SetInferShape(ge::InferShape);
        this->SetInferDataType(ge::InferDataType);
        this->EnableFallBack();
        this->MC2().HcclGroup("group");
        this->MC2().HcclServerType(HcclServerType::AICPU);
    }
};

OP_ADD(ReduceScatterCustomV3);
IMPL_OP(ReduceScatterCustomV3).CalcOpParam(ge::CommonKFCMc2CalcParamFunc).GenerateTask(ge::CommonKFCMc2GenTask);

} // namespace ops
