/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_REP_REFERENCE_MANAGER_H
#define CCU_REP_REFERENCE_MANAGER_H

#include <unordered_map>
#include <vector>
#include <memory>

#include "hcomm_ccu_res.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/common/ccu_kernel_resource.h"

namespace hcomm {
namespace CcuRep {

// 支持自定义算子CCU开发资源管理优化，减少预留资源数量，避免xn耗尽。
// FUNC_ARG_MAX：单个 ccu::Func 入参/出参上限。每提升 1，每个 die 在
// CcuRepReferenceManager::GetResReq 中多预留 2 个 Xn（funcInVar + funcOutVar）。
constexpr uint16_t FUNC_ARG_MAX = 1;
constexpr uint16_t FUNC_NEST_MAX = 1;
constexpr uint16_t FUNC_CALL_LAYER_INVALID = 0xFFFF;

// GetResReq 申请的 XN 数必须与 hcomm 侧 RegisterContext 为 reference manager 预留的数量一致，
// 否则 translatorResources 数量不匹配会导致资源映射错位且不报错。
// 提升 FUNC_ARG_MAX 或 FUNC_NEST_MAX 时，必须同步修改 pkg_inc/hcomm/hcomm_ccu_res.h 中的
// HCOMM_CCU_REFERENCE_MANAGER_XN_NUM。
static_assert(FUNC_ARG_MAX + FUNC_ARG_MAX + 1 + FUNC_NEST_MAX + 1 == HCOMM_CCU_REFERENCE_MANAGER_XN_NUM,
    "reference manager XN count must match HCOMM_CCU_REFERENCE_MANAGER_XN_NUM in hcomm_ccu_res.h");

class CcuRepReferenceManager {
public:
    explicit CcuRepReferenceManager(uint8_t deiId);
    static HcommCcuResRequestPod GetResReq(uint8_t reqDieId);
    void GetRes(CcuRepResource& res);
    std::shared_ptr<CcuRepBlock> GetRefBlock(const std::string& label);
    void SetRefBlock(const std::string& label, std::shared_ptr<CcuRepBlock> refBlock);
    uint16_t GetFuncAddr(const std::string& label);
    const Variable& GetFuncCall();
    const Variable& GetFuncRet(uint16_t callLayer);
    const std::vector<Variable>& GetFuncIn();
    const std::vector<Variable>& GetFuncOut();
    void Dump() const;
    void ClearRepReference();

private:
    bool CheckValid(const std::string& label);
    bool CheckUnique(const std::string& label);

private:
    uint8_t dieId{0};
    std::unordered_map<std::string, std::shared_ptr<CcuRepBlock>> referenceMap;
    std::vector<Variable> funcCallVar;
    std::vector<Variable> funcInVar;
    std::vector<Variable> funcOutVar;
};

}; // namespace CcuRep
}; // namespace hcomm

#endif // _CCU_REP_REFERENCE_MANAGER_H
