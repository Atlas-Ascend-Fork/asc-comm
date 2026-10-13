/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_KERNEL_MGR_H
#define ASCCOMM_CCU_KERNEL_MGR_H

#include <array>
#include <mutex>
#include <unordered_map>

#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/common/asc_ccu_res_snapshot.h"
#include "hcomm/hcomm_ccu_dfx.h" // asccomm_ccu_diagnose 的请求/回调类型（DFX 诊断专用头）

#include "hcomm/resource/representation/reps/translator/ccu_rep_translator_v1.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace asc {

int32_t asccomm_ccu_diagnose(const HcommCcuDfxRequest* request, HcommCcuDfxEmitFn emit, void* context);

using namespace ccu_rep;

class ccu_kernel_mgr {
public:
    static ccu_kernel_mgr& get_instance(int32_t device_logic_id);

    HcclResult init();
    HcclResult deinit();
    CcuResult configure(asc_ccu_res_snapshot& res_pack);

    CcuResult Register(
        asc_ccu_res_snapshot& res_pack, uint32_t die_id, const char* kernel_func_name, const void* kernel_func,
        const void** kernel_args, const uint32_t arg_num, ccu_kernel_handle& kernel_handle);

    CcuResult translate(const std::vector<ccu_kernel_handle>& kernel_handles);

    ccu_kernel* get_kernel(ccu_kernel_handle kernel_handle);
    CcuResult get_ccu_kernel_info(ccu_kernel_handle kernel_handle, ccu_kernel_info& info);
    CcuResult un_register(ccu_kernel_handle kernel_handle);

    ccu_kernel* get_current_kernel();
    const uint64_t* get_control_ops() const; // ascCustom 槽位数组

private:
    explicit ccu_kernel_mgr() = default;
    ~ccu_kernel_mgr();

    ccu_kernel_mgr(const ccu_kernel_mgr& that) = delete;
    ccu_kernel_mgr& operator=(const ccu_kernel_mgr& that) = delete;

private:
    CcuResult prepare_const_value_resources();
    CcuResult alloc_res(asc_ccu_res_snapshot& res_pack);

    HcclResult instantiation_translator(uint16_t die_id, asc_ccu_res_snapshot& res_pack);
    HcclResult trans_rep_sequence_to_microcode(const std::vector<ccu_kernel*>& kernels, bool is_func_block);
    HcclResult load_instruction(const ccu_rep::ccu_instr_info& instr_info, const uint32_t die_id);

private:
    bool initialized_flag_{false};
    int32_t dev_logic_id_{-1};
    std::mutex kernel_map_mutex_{};
    std::mutex translate_mutex_{};
    ccu_kernel_handle kernel_id_ = 0;
    std::unordered_map<ccu_kernel_handle, std::unique_ptr<ccu_kernel>> kernel_map_{};

    std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::shared_ptr<ccu_rep::ccu_rep_translator>>>
        translators_;
    std::unordered_map<uint16_t, std::unordered_map<uint16_t, std::shared_ptr<ccu_rep::ccu_rep_reference_manager>>>
        reference_mgrs_;
    std::unique_ptr<ccu_kernel> curr_kernel_{nullptr};
    std::shared_ptr<ccu_ins_generater_base> ins_gene_ptr_;
    uint32_t ccu_version_{HCOMM_CCU_VERSION_INVALID};
    std::array<uint32_t, HCOMM_CCU_MAX_DIE_NUM> mission_keys_{};
    uint64_t control_ops_[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]{};
};
}; // namespace asc
#endif // HCOMM_CCU_KERNEL_MGR_IMP_H
