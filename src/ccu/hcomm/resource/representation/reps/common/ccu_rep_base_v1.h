/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REPRESENTATION_BASE
#define CCU_REPRESENTATION_BASE

#include <string>

#include "hcomm/common/ccu_common.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"

namespace asc {

class ccu_kernel;

namespace ccu_rep {

class ccu_ins_generater_base;
class ccu_ins_generater_v1;

struct trans_dep {
    int32_t logical_id;
    uint16_t die_id;
    uint16_t reserve_xn_id;
    uint16_t reserve_gsa_id;
    uint16_t reserve_cke_id;
    uint16_t reserve_channal_id[2]; //  0: selfLoopBack; 1: inter die, 0xffff为无效值，rep翻译时检查
    uint64_t xn_base_addr[ccu_max_iodie_num];
    uint64_t ccu_res_space_token_info;
    uint64_t mem_token_info;
    uint16_t comm_xn[3];  // 3个Xn
    uint16_t comm_gsa[2]; // 2个GSA
    uint16_t comm_signal; // 1个CKE
    uint16_t load_xn_id;
    bool is_func_block;
};

class ccu_rep_base {
public:
    explicit ccu_rep_base();
    virtual ~ccu_rep_base();
    virtual bool translate(ccu_kernel* ccu_kernel, ccu_instr*& instr, uint16_t& instr_id, const trans_dep& dep) = 0;
    virtual std::string describe() = 0;
    virtual uint32_t get_id() { return 0; }

    ccu_rep_type type() const;
    bool translated() const;
    uint16_t start_instr_id() const;
    virtual uint16_t instr_count();

protected:
    ccu_rep_type type_{ccu_rep_type::base};
    bool translated_{false};
    uint16_t instr_id_{0};
    uint16_t instr_count_{0};
};

}; // namespace ccu_rep
}; // namespace asc
#endif // _CCU_REPRESENTATION_BASE
