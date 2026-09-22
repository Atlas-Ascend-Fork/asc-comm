/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REP_TRANSLATOR_H
#define ASCCOMM_CCU_REP_TRANSLATOR_H

#include <memory>
#include <vector>
#include <functional>

#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/resource/microcode/ccu_instr_info_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asc {
namespace ccu_rep {

constexpr uint32_t ccu_translator_mission_slot_num = 16;
constexpr int ccu_translator_xn_num = 4;
constexpr int ccu_translator_gsa_num = 3;
constexpr int ccu_translator_cke_num = 2;

class ccu_rep_translator {
public:
    ccu_rep_translator(std::shared_ptr<ccu_rep_reference_manager> ref_manager, const trans_dep& trans_dep);
    static void set_ccu_version(uint32_t version);
    static uint32_t get_instr_num();
    void get_res(ccu_rep_resource& res);
    ccu_instr_info translate(
        ccu_kernel* ccu_kernel, const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, uint16_t start_instr_id,
        bool is_func_block = false);
    void translate(
        ccu_kernel* ccu_kernel, const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, ccu_instr*& instr,
        uint16_t& instr_id, std::function<bool(std::shared_ptr<ccu_rep_base>)> filter);
    void dump_instruction(const ccu_instr_info& instr_info) const;
    void set_trans_dep(trans_dep trans_dep_in) { trans_dep_ = trans_dep_in; }
    trans_dep& get_trans_dep() { return trans_dep_; }

private:
    template <typename t1, typename t2>
    void build_reference(const std::shared_ptr<ccu_rep_base>& rep);
    void pre_process(std::shared_ptr<ccu_rep_base> rep);
    void common_process(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id);
    void finish_main_block(ccu_instr*& instr, uint16_t& instr_id);
    void dump_rep(const std::vector<std::shared_ptr<ccu_rep_base>>& rep_vec, const ccu_instr_info& instr_info) const;
    void bind_resource(bool is_func_block);

private:
    static const int xn_num = ccu_translator_xn_num;
    static const int gsa_num = ccu_translator_gsa_num;
    static const int cke_num = ccu_translator_cke_num;
    std::shared_ptr<ccu_rep_reference_manager> ref_manager_{nullptr};
    variable var_[xn_num];
    address addr_[gsa_num];
    completed_event signal_[cke_num]; // Uses discrete CKE resources.
    trans_dep trans_dep_{0};
    static uint32_t ccu_version;
};
}; // namespace ccu_rep
}; // namespace asc

#endif // HCCL_CCU_REP_TRANSLATOR_H
