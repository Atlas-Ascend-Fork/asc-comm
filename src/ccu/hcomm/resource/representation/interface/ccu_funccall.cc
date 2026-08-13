/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"

#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asccomm {
namespace CcuRep {

FuncCall::FuncCall(CcuRepContext* context, std::string label) : context(context), label(label)
{
    CcuInsGeneraterBase* insGenPtr = context->GetInsGenerator();
    asccomm::CheckCcuNotNull(insGenPtr, "[FuncCall::FuncCall](label) insGenPtr is nullptr!");
    repFuncCall = std::make_shared<CcuRepFuncCall>(insGenPtr, label);
}

FuncCall::FuncCall(CcuRepContext* context, const Variable& funcAddr) : context(context)
{
    CcuInsGeneraterBase* insGenPtr = context->GetInsGenerator();
    asccomm::CheckCcuNotNull(insGenPtr, "[FuncCall::FuncCall](funcAddr) insGenPtr is nullptr!");
    repFuncCall = std::make_shared<CcuRepFuncCall>(insGenPtr, funcAddr);
}

void FuncCall::AppendToContext()
{
    if (context == nullptr) {
        asccomm::ThrowCcuInternal("context is nullptr, func call, append to context");
    }
    return context->Append(repFuncCall);
}

}; // namespace CcuRep
}; // namespace asccomm
