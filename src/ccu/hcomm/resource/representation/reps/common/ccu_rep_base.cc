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

namespace asc {
namespace ccu_rep {

ccu_rep_base::ccu_rep_base() {}

ccu_rep_base::~ccu_rep_base() {}

ccu_rep_type ccu_rep_base::type() const { return type_; }

bool ccu_rep_base::translated() const { return translated_; }

uint16_t ccu_rep_base::start_instr_id() const { return instr_id_; }
uint16_t ccu_rep_base::instr_count() { return instr_count_; }

}; // namespace ccu_rep
}; // namespace asc
