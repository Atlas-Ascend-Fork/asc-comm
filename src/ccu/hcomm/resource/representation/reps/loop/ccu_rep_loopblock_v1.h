/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_REPRESENTATION_LOOP_BLOCK_H
#define ASCCOMM_CCU_REPRESENTATION_LOOP_BLOCK_H

#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_arg_v1.h"

namespace asc {
namespace ccu_rep {

class ccu_rep_loop_block : public ccu_rep_block {
public:
    explicit ccu_rep_loop_block(ccu_ins_generater_base* ins_gen_ptr, const std::string& label);
    std::string describe() override;

    void define_arg(variable var);
    void define_arg(memory mem);
    void define_arg(local_addr addr);
    void define_arg(remote_addr addr);

    void define_arg(const std::vector<variable> var_list);
    void define_arg(const std::vector<memory> mem_list);
    void define_arg(const std::vector<local_addr> addr_list);
    void define_arg(const std::vector<remote_addr> addr_list);

    ccu_rep_arg& get_arg(uint16_t index);

private:
    std::vector<ccu_rep_arg> args_;
};

}; // namespace ccu_rep
}; // namespace asc
#endif // ASCCOMM_CCU_REPRESENTATION_LOOP_BLOCK_H
