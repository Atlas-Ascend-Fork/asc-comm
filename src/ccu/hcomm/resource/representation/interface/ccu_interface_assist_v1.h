/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_INTERFACE_H
#define CCU_INTERFACE_H

#include <memory>

#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

void append_to_context(ccu_rep_context* context, std::shared_ptr<ccu_rep::ccu_rep_base> rep);
std::shared_ptr<ccu_rep::ccu_rep_block> current_block(ccu_rep_context* context);
void set_current_block(ccu_rep_context* context, std::shared_ptr<ccu_rep::ccu_rep_block> rep_block);
variable create_variable(ccu_rep_context* context);

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_INTERFACE_H
