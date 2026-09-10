/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_JUMPLABEL_H
#define CCU_REPRESENTATION_JUMPLABEL_H

#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_jump_label : public ccu_rep_block {
public:
    explicit ccu_rep_jump_label(ccu_ins_generater_base* ins_generator_ptr, const std::string& label);
    std::string describe() override;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_JUMPLABEL_H
