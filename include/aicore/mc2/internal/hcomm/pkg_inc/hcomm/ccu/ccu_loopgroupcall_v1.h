/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#ifndef CCU_LOOPGROUP_CALL_H
#define CCU_LOOPGROUP_CALL_H

#include <vector>
#include <string>

#include "ccu_datatype_v1.h"
#include "ccu_loopcall_v1.h"
#include "ccu_rep_context_v1.h"
#include "ccu_rep_loopblock_v1.h"

namespace hcomm {
namespace CcuRep {

class LoopGroupCall {
public:
    explicit LoopGroupCall(CcuRepContext *context, std::string label = "") : context(context), label(label)
    {
    }
    void Run(const std::vector<LoopCall> &loopVec, const std::vector<Variable> &loopCfg,
             const std::vector<Executor> &executors, Variable paraCfgIn, Variable offsetCfgIn) const;

private:
    CcuRepContext *context;
    std::string    label;

    uint64_t paraCfg{0};
    uint64_t offsetCfg{0};
};

}; // namespace CcuRep
}; // namespace hcomm
#endif // _CCU_LOOPGROUP_CALL_H