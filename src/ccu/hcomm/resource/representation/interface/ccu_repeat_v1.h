/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPEAT_H
#define CCU_REPEAT_H

#include "hcomm/resource/representation/reps/control/ccu_rep_jump_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_jumplabel_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {

#define CCU_WHILE(x) \
    for (auto __ccuRepeatHidden = CcuRep::Repeat(this, x); __ccuRepeatHidden.check(); __ccuRepeatHidden.Run())
#define CCU_BREAK __ccuRepeatHidden.Break()

class repeat {
public:
    repeat(ccu_rep_context* context, ccu_relational_operator<variable, uint64_t> rel);
    ~repeat();
    void Break() const;
    bool check() const;
    void run();

private:
    ccu_rep_context* context_{nullptr};
    bool is_executed_{false};

    std::shared_ptr<ccu_rep_jump_base> jump_{nullptr};
    std::shared_ptr<ccu_rep_jump_label> begin_label_{nullptr};
    std::shared_ptr<ccu_rep_jump_label> end_label_{nullptr};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPEAT_H
