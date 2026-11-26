/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "../op_kernel/all_reduce_ccu_bench_tiling.h"
#include "register/op_def_registry.h"
#include "tiling/platform/platform_ascendc.h"
#include "tiling/tiling_api.h"
#include "ccu_collective_common.h"

namespace optiling {

static ge::graphStatus AllReduceCustomTilingFunc(gert::TilingContext* context)
{
    if (CheckCollectiveInputs(context, 5) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    const char* group = context->GetAttrs()->GetAttrPointer<char>(0);
    const int64_t reduce_type = *context->GetAttrs()->GetAttrPointer<int64_t>(1);
    if (group == nullptr || reduce_type < 0 || reduce_type > 3) {
        return ge::GRAPH_FAILED;
    }

    const auto input_type = context->GetInputDesc(0)->GetDataType();
    const auto output_type = context->GetOutputDesc(0)->GetDataType();
    context->SetBlockDim(MC2_CCU_BLOCK_DIM);
    context->SetTilingKey(MC2_CCU_TILING_KEY);
    const uint64_t lib_workspace_size =
        platform_ascendc::PlatformAscendC(context->GetPlatformInfo()).GetLibApiWorkSpaceSize();
    context->GetWorkspaceSizes(1)[0] = lib_workspace_size;

    auto* tiling = context->GetTilingData<AllReduceCcuBenchTilingData>();
    if (tiling == nullptr) {
        return ge::GRAPH_FAILED;
    }
    tiling->param.count = GetInputElementCount(context);
    tiling->param.reduceType = static_cast<uint32_t>(reduce_type);
    tiling->param.dataType = GetHcclDataType(input_type);

    AscendC::Mc2CcTilingConfig config(
        group, HCCL_CMD_ALLREDUCE, "", reduce_type, GetHcclDataType(output_type), GetHcclDataType(input_type));
    config.SetReduceType(reduce_type, GetHcclDataType(output_type), GetHcclDataType(input_type));
    if (ApplyRuntimeCcuConfig(config, context, 5) != ge::GRAPH_SUCCESS) {
        return ge::GRAPH_FAILED;
    }
    if (config.GetTiling(tiling->mc2InitTiling) != 0 || config.GetTiling(tiling->mc2CcTiling) != 0) {
        return ge::GRAPH_FAILED;
    }
    return ge::GRAPH_SUCCESS;
}

} // namespace optiling

namespace ge {

static graphStatus InferShape(gert::InferShapeContext* context)
{
    if (context == nullptr || context->GetInputShape(0) == nullptr || context->GetOutputShape(0) == nullptr ||
        context->GetAttrs() == nullptr || context->GetInputShape(0)->GetDimNum() != 2) {
        return GRAPH_FAILED;
    }
    *context->GetOutputShape(0) = *context->GetInputShape(0);
    return GRAPH_SUCCESS;
}

static graphStatus InferDataType(gert::InferDataTypeContext* context)
{
    context->SetOutputDataType(0, context->GetInputDataType(0));
    return GRAPH_SUCCESS;
}

} // namespace ge

namespace ops {

class AllReduceCcuBench : public OpDef {
public:
    explicit AllReduceCcuBench(const char* name) : OpDef(name)
    {
        this->Input("x1")
            .ParamType(REQUIRED)
            .DataType(MC2_CCU_DTYPES)
            .Format(MC2_CCU_FORMATS)
            .UnknownShapeFormat(MC2_CCU_FORMATS);
        this->Output("y")
            .ParamType(REQUIRED)
            .DataType(MC2_CCU_DTYPES)
            .Format(MC2_CCU_FORMATS)
            .UnknownShapeFormat(MC2_CCU_FORMATS);
        this->Attr("group").AttrType(REQUIRED).String();
        this->Attr("reduceOp").AttrType(REQUIRED).Int();
        this->Attr("rank_size").AttrType(OPTIONAL).Int(8);
        this->Attr("tileM").AttrType(REQUIRED).Int();
        this->Attr("commTurn").AttrType(OPTIONAL).Int(0);
        this->Attr("algorithm").AttrType(OPTIONAL).String("");
        this->Attr("comm_engine").AttrType(REQUIRED).Int();
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
        this->AICore().SetTiling(optiling::AllReduceCustomTilingFunc);
        this->AICore().AddConfig("ascend950", config);
        this->SetInferShape(ge::InferShape);
        this->SetInferDataType(ge::InferDataType);
        this->EnableFallBack();
        this->MC2().HcclGroup("group");
        this->MC2().HcclServerType(HcclServerType::CCU);
    }
};

OP_ADD(AllReduceCcuBench);
IMPL_OP(AllReduceCcuBench).CalcOpParam(ge::CommonCcuMc2CalcParamFunc).GenerateTask(ge::CommonCcuMc2GenTask);

} // namespace ops
