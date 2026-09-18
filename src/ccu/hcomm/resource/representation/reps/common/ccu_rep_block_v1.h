/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_BLOCK_H
#define CCU_REPRESENTATION_BLOCK_H

#include <vector>
#include <memory>

#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_block : public ccu_rep_base {
public:
    explicit ccu_rep_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label = "");
    bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) override;
    std::string describe() override;
    uint16_t instr_count() override;
    const std::string& get_label() const;
    std::vector<std::shared_ptr<ccu_rep_base>>& get_reps();
    void append(std::shared_ptr<ccu_rep_base> rep);
    std::shared_ptr<ccu_rep_base> get_rep_by_instr_id(uint16_t instr_id);

protected:
    ccu_ins_generater_base* ins_generator_ptr_{nullptr};

private:
    std::string label_;
    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec_;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_BLOCK_H
