/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"
#include "ccu_kernel.h"
#include "ccu_interface_assist_v1.h"

#include "string_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

FuncBlock::FuncBlock(CcuRepContext *context, std::string label, uint16_t callLayer)
    : context(context), label(label), callLayer(callLayer)
{
    repFuncBlock = std::make_shared<CcuRepFuncBlock>(label);
    AppendToContext(context, repFuncBlock);

    curActiveBlock = CurrentBlock(context);

    SetCurrentBlock(context, repFuncBlock);

    HCCL_INFO("Enter block %s, last block %s", repFuncBlock->Describe().c_str(), curActiveBlock->Describe().c_str());
}

FuncBlock::~FuncBlock()
{
    repFuncBlock->SetCallLayer(callLayer);
    SetCurrentBlock(context, curActiveBlock);
}

}; // namespace CcuRep
}; // namespace hcomm