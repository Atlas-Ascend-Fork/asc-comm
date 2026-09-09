/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_CONDITION_H
#define CCU_CONDITION_H

#include "hcomm/resource/representation/reps/control/ccu_rep_jump_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_jumplabel_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {

#define CCU_IF(x) CCU_IF_HELPER1(__COUNTER__, x)

#define CCU_IF_HELPER1(ctr, x) CCU_IF_HELPER2(ctr, x)

#define CCU_IF_HELPER2(ctr, x)                                                                           \
    for (auto __ccuConditionHidden##ctr = CcuRep::Condition(this, x); __ccuConditionHidden##ctr.check(); \
         __ccuConditionHidden##ctr.Run())

class condition {
public:
    condition(ccu_rep_context* context, ccu_relational_operator<variable, uint64_t> rel);
    ~condition();
    bool check() const;
    void run();

private:
    ccu_rep_context* context_{nullptr};
    bool is_executed_{false};

    std::shared_ptr<ccu_rep_jump_base> jump_{nullptr};
    std::shared_ptr<ccu_rep_jump_label> end_label_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_CONDITION_H
