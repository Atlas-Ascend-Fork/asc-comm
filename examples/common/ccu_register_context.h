/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXAMPLES_CCU_REGISTER_CONTEXT_H
#define ASCCOMM_EXAMPLES_CCU_REGISTER_CONTEXT_H

#include <vector>

#include <hccl/hccl_ccu_res.h>

namespace asccomm_examples {

struct CcuRegisterContextStorage {
    HcommCcuRegisterContextPod context{};
    std::vector<HcommCcuResRangePod> instanceRanges;
    std::vector<HcommCcuResRangePod> translatorRanges;
    std::vector<HcommCcuResRangePod> instructionRanges;
};

inline HcclResult QueryCcuRegisterContext(HcclComm comm, CcuRegisterContextStorage &storage)
{
    storage.context.instanceResources = {nullptr, 0, 0};
    storage.context.translatorResources = {nullptr, 0, 0};
    storage.context.instructionResources = {nullptr, 0, 0};
    HcclResult ret = HcclCommQueryCcuRegisterContext(comm, &storage.context);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    storage.instanceRanges.resize(storage.context.instanceResources.count);
    storage.translatorRanges.resize(storage.context.translatorResources.count);
    storage.instructionRanges.resize(storage.context.instructionResources.count);
    storage.context.instanceResources = {
        storage.instanceRanges.data(), static_cast<uint32_t>(storage.instanceRanges.size()), 0};
    storage.context.translatorResources = {
        storage.translatorRanges.data(), static_cast<uint32_t>(storage.translatorRanges.size()), 0};
    storage.context.instructionResources = {
        storage.instructionRanges.data(), static_cast<uint32_t>(storage.instructionRanges.size()), 0};
    return HcclCommQueryCcuRegisterContext(comm, &storage.context);
}

} // namespace asccomm_examples

#endif // ASCCOMM_EXAMPLES_CCU_REGISTER_CONTEXT_H
