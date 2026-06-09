/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "hcclCommOp.h"
namespace hccl {

std::shared_ptr<Hccl::DfxOpInfo> ConvertToDfxOpInfo(const HcclDfxOpInfo& dfxOpInfo) {
    auto dfxOpInfoOnce = std::make_shared<Hccl::DfxOpInfo>();
    Hccl::CollOperator collOp{};
    collOp.opMode = static_cast<Hccl::OpMode::Value>(dfxOpInfo.opMode); 
    if (Hccl::OP_TYPE_MAP.find(static_cast<HcclCMDType>(dfxOpInfo.opType)) == Hccl::OP_TYPE_MAP.end()) {
        HCCL_ERROR("%s static_cast<HcclCMDType>(dfxOpInfo.opType)[%d] is not supported.", __func__, static_cast<HcclCMDType>(dfxOpInfo.opType));
        return {};
    }
    collOp.opType = Hccl::OP_TYPE_MAP.at(static_cast<HcclCMDType>(dfxOpInfo.opType));
    collOp.reduceOp = Hccl::HcclReduceOpToReduceOp(static_cast<HcclReduceOp>(dfxOpInfo.reduceOp));
    collOp.dataType = Hccl::HcclDataTypeToDataType(static_cast<HcclDataType>(dfxOpInfo.dataType));
    collOp.dataCount = dfxOpInfo.dataCount;
    collOp.root = dfxOpInfo.root;
    collOp.staticAddr = false;
    collOp.staticShape = false;
    collOp.inputMem = std::make_shared<Hccl::Buffer>(0, 0);
    collOp.outputMem = std::make_shared<Hccl::Buffer>(0, 0);
    collOp.scratchMem = std::make_shared<Hccl::Buffer>(0, 0);

    dfxOpInfoOnce->op_= std::move(collOp);
    dfxOpInfoOnce->algTag_ = dfxOpInfo.algTag;
    dfxOpInfoOnce->algType_ = Hccl::AlgType{Hccl::AlgType::MESH}.Describe();
    dfxOpInfoOnce->tag_ = Hccl::OpTypeToString(collOp.opType);
    dfxOpInfoOnce->beginTime_ = dfxOpInfo.beginTime;
    dfxOpInfoOnce->cpuWaitAicpuNotifyId_ = dfxOpInfo.cpuWaitAicpuNotifyId;

    return dfxOpInfoOnce;
}

}