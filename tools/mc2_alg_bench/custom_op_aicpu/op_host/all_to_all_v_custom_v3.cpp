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

#include "../op_kernel/all_to_all_v_custom_v3_tiling.h"
#include "register/op_def_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "tiling/tiling_api.h"
#include "common.h"

namespace optiling {

constexpr int MAX_RANK_DIM = 384;

static ge::graphStatus AllToAllVCustomTilingFunc(gert::TilingContext* context)
{
    if (CheckPerfTensor(context) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    const char* group = context->GetAttrs()->GetAttrPointer<char>(0);
    const int step_size = *context->GetAttrs()->GetAttrPointer<int>(1);
    const int rank_size = *context->GetAttrs()->GetAttrPointer<int>(2);
    const bool sqrt1_flag = *context->GetAttrs()->GetAttrPointer<bool>(4);
    const bool sqrt2_flag = *context->GetAttrs()->GetAttrPointer<bool>(5);
    const uint64_t input_count = GetInputElementCount(context);
    if (group == nullptr || rank_size <= 0 || rank_size > MAX_RANK_DIM || step_size != 0 || sqrt1_flag || sqrt2_flag ||
        input_count % static_cast<uint64_t>(rank_size) != 0) {
        ERROR_LOG("AllToAllV perf mode requires equal splits, stepSize=0 and sqrt flags disabled");
        return ge::GRAPH_FAILED;
    }

    const auto input_type = context->GetInputDesc(0)->GetDataType();
    const auto output_type = context->GetOutputDesc(0)->GetDataType();
    context->SetBlockDim(HCCL_API_TEST_BLOCK_DIM);
    context->SetTilingKey(HCCL_API_TEST_TILING_KEY);
    const uint64_t lib_workspace_size =
        platform_ascendc::PlatformAscendC(context->GetPlatformInfo()).GetLibApiWorkSpaceSize();
    context->GetWorkspaceSizes(1)[0] = lib_workspace_size;

    auto* tiling = context->GetTilingData<AllToAllVCustomV3TilingData>();
    tiling->param.peerCount = input_count / static_cast<uint64_t>(rank_size);
    tiling->param.rankDim = static_cast<uint32_t>(rank_size);
    tiling->param.dataType = GetHcclDataType(input_type);

    AscendC::Mc2CcTilingConfig config(
        group, HCCL_CMD_ALLTOALLV, "", 0, GetHcclDataType(output_type), GetHcclDataType(input_type));
    config.SetStepSize(0);
    config.SetCommEngine(HCCL_API_TEST_AICPU_TS_COMM_ENGINE);
    if (ApplyRuntimeAlgConfig(config, context, 6) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    config.GetTiling(tiling->mc2InitTiling);
    config.GetTiling(tiling->mc2CcTiling);
    // INFO_LOG("AllToAllV equalPeerCount=%llu, rankSize=%d, blockDim=1, workingCore=AIC0",
    //     static_cast<unsigned long long>(tiling->param.peerCount), rank_size);
    return ge::GRAPH_SUCCESS;
}

} // namespace optiling

namespace ge {

static graphStatus InferShape(gert::InferShapeContext* context)
{
    const auto* input = context->GetInputShape(0);
    const int out_dim0 = *context->GetAttrs()->GetAttrPointer<int>(2);
    auto* output = context->GetOutputShape(0);
    output->SetDimNum(0);
    output->AppendDim(input->GetDim(0) < 0 ? input->GetDim(0) : out_dim0);
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

class AllToAllVCustomV3 : public OpDef {
public:
    explicit AllToAllVCustomV3(const char* name) : OpDef(name)
    {
        this->Input("x1")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Input("input_splits")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_SPLIT_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Input("output_splits")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_SPLIT_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Output("y")
            .ParamType(REQUIRED)
            .DataType(HCCL_API_TEST_DTYPES)
            .Format(HCCL_API_TEST_FORMATS)
            .UnknownShapeFormat(HCCL_API_TEST_FORMATS);
        this->Attr("group").AttrType(REQUIRED).String();
        this->Attr("step_size").AttrType(OPTIONAL).Int(0);
        this->Attr("out_dim0").AttrType(OPTIONAL).Int(0);
        this->Attr("tile_k").AttrType(OPTIONAL).Int(512);
        this->Attr("sqrt1_flag").AttrType(OPTIONAL).Bool(false);
        this->Attr("sqrt2_flag").AttrType(OPTIONAL).Bool(false);
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
        this->AICore().SetTiling(optiling::AllToAllVCustomTilingFunc);
        this->AICore().AddConfig("ascend950", config);
        this->SetInferShape(ge::InferShape);
        this->SetInferDataType(ge::InferDataType);
        this->EnableFallBack();
        this->MC2().HcclGroup("group");
        this->MC2().HcclServerType(HcclServerType::AICPU);
    }
};

OP_ADD(AllToAllVCustomV3);
IMPL_OP(AllToAllVCustomV3).CalcOpParam(ge::CommonKFCMc2CalcParamFunc).GenerateTask(ge::CommonKFCMc2GenTask);

} // namespace ops
