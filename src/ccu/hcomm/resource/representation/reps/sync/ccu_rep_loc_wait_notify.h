/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_NOTIFY_H
#define ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_NOTIFY_H
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loc_wait_notify : public ccu_rep_base {
public:
    ccu_rep_loc_wait_notify(
        ccu_ins_generater_base* ins_gen_ptr, const local_notify& notify, const uint32_t mask, bool is_profiling = true);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint32_t get_mask() const { return mask_; };
    uint16_t get_notify_id() { return notify_.id(); };

    local_notify get_notify() { return notify_; }

    bool get_is_profiling() const { return is_profiling_; }

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    local_notify notify_{};
    uint32_t mask_{0};
    bool is_profiling_{true};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_NOTIFY_H
