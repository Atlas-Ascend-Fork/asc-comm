/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_EVENT_H
#define ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_EVENT_H

#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loc_wait_event : public ccu_rep_base {
public:
    // mask 由调用方显式传入（与 CompletedEvent 解耦）。
    ccu_rep_loc_wait_event(
        ccu_ins_generater_base* ins_gen_ptr, const completed_event& event, uint32_t mask, bool is_profiling = true);
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint32_t get_mask() const { return mask_; };
    uint32_t get_event_id() { return event_.id(); };

    completed_event get_event() { return event_; }

    bool get_is_profiling() const { return is_profiling_; }
    void set_dependency_info(const std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>>& dep_info);
    std::vector<std::shared_ptr<ccu_rep_base>> get_dependency_info(uint32_t bit);

private:
    ccu_ins_generater_base* ins_gen_ptr_{nullptr};
    completed_event event_{};
    uint32_t mask_{1};
    bool is_profiling_{true};
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>> dep_info_;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOC_WAIT_EVENT_H
