/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXAMPLES_CCU_USER_RESOURCE_CONTEXT_H
#define ASCCOMM_EXAMPLES_CCU_USER_RESOURCE_CONTEXT_H

#include <cstdint>
#include <vector>

#include <hccl/hccl_types.h>
#include <hcomm/hcomm_ccu_res.h>

#include "ccu_register_context.h"

namespace asccomm_examples {

constexpr uint32_t CCU_CHANNEL_RESERVED_RESOURCE_START_ID = 0;
constexpr uint32_t CCU_CHANNEL_RESERVED_XN_COUNT = 4;
constexpr uint32_t CCU_CHANNEL_RESERVED_CKE_COUNT = 4;
constexpr uint32_t CCU_MISSION_DEFAULT_COUNT = 16;
constexpr uint32_t CCU_MS_DEFAULT_COUNT = 1536;
constexpr uint32_t CCU_CKE_DEFAULT_COUNT = 1024;
constexpr uint32_t CCU_V1_LOOP_DEFAULT_COUNT = 200;
constexpr uint32_t CCU_V1_XN_DEFAULT_COUNT = 3072;
constexpr uint32_t CCU_V1_GSA_DEFAULT_COUNT = 3072;
constexpr uint32_t CCU_V2_LOOP_DEFAULT_COUNT = 512;
constexpr uint32_t CCU_V2_XN_DEFAULT_COUNT = 12288;
constexpr uint32_t CCU_V2_GSA_DEFAULT_COUNT = 0;
constexpr uint32_t CCU_VERSION_V1 = 0;
constexpr uint32_t CCU_VERSION_V2 = 1;

struct CcuUserResourceContextStorage {
    HcommCcuRegisterContextPod context{};
    std::vector<HcommCcuResRangePod> instanceRanges;
    std::vector<HcommCcuResRangePod> translatorRanges;
};

inline void AddRange(std::vector<HcommCcuResRangePod>& ranges, uint32_t type, uint32_t dieId,
    uint32_t startId, uint32_t count)
{
    if (count != 0) {
        ranges.push_back({type, dieId, startId, count});
    }
}

inline HcclResult AddDefaultRangesForDie(
    const CcuRegisterContextStorage& hcommContextStorage, CcuUserResourceContextStorage& storage, uint32_t dieId)
{
    const bool isV2 = storage.context.ccuVersion == CCU_VERSION_V2;
    const uint32_t loopCount = isV2 ? CCU_V2_LOOP_DEFAULT_COUNT : CCU_V1_LOOP_DEFAULT_COUNT;
    const uint32_t xnCount = isV2 ? CCU_V2_XN_DEFAULT_COUNT : CCU_V1_XN_DEFAULT_COUNT;
    const uint32_t gsaCount = isV2 ? CCU_V2_GSA_DEFAULT_COUNT : CCU_V1_GSA_DEFAULT_COUNT;

    if (xnCount <= CCU_CHANNEL_RESERVED_XN_COUNT || CCU_CKE_DEFAULT_COUNT <= CCU_CHANNEL_RESERVED_CKE_COUNT) {
        return HCCL_E_PARA;
    }

    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_BLOCK_LOOP, dieId, 0, loopCount);
    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_BLOCK_MS, dieId, 0, CCU_MS_DEFAULT_COUNT);
    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_BLOCK_XN, dieId,
        CCU_CHANNEL_RESERVED_RESOURCE_START_ID + CCU_CHANNEL_RESERVED_XN_COUNT,
        xnCount - CCU_CHANNEL_RESERVED_XN_COUNT);
    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_BLOCK_CKE, dieId,
        CCU_CHANNEL_RESERVED_RESOURCE_START_ID + CCU_CHANNEL_RESERVED_CKE_COUNT,
        CCU_CKE_DEFAULT_COUNT - CCU_CHANNEL_RESERVED_CKE_COUNT);
    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_BLOCK_GSA, dieId, 0, gsaCount);
    AddRange(storage.instanceRanges, HCOMM_CCU_BATCH_RES_MISSION, dieId, 0, CCU_MISSION_DEFAULT_COUNT);

    storage.context.die[dieId] = hcommContextStorage.context.die[dieId];
    return HCCL_SUCCESS;
}

inline HcclResult AddInstructionRangesFromHcommContext(
    const CcuRegisterContextStorage& hcommContextStorage, CcuUserResourceContextStorage& storage)
{
    const HcommCcuResRangeViewPod& view = hcommContextStorage.context.instanceResources;
    bool hasInstructionRange = false;
    for (uint32_t index = 0; index < view.count; ++index) {
        const HcommCcuResRangePod& range = view.ranges[index];
        if (range.resourceType != HCOMM_CCU_RES_INSTRUCTION) {
            continue;
        }
        storage.instanceRanges.push_back(range);
        hasInstructionRange = true;
    }
    return hasInstructionRange ? HCCL_SUCCESS : HCCL_E_PARA;
}

inline HcclResult BuildDefaultUserCcuRegisterContext(
    const CcuRegisterContextStorage& hcommContextStorage, CcuUserResourceContextStorage& storage)
{
    const HcommCcuRegisterContextPod& hcommContext = hcommContextStorage.context;
    if (hcommContext.header.version != HCOMM_CCU_RES_ABI_VERSION ||
        hcommContext.header.magicWord != HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD ||
        hcommContext.header.size != sizeof(HcommCcuRegisterContextPod) ||
        hcommContext.header.reserved != 0 ||
        (hcommContext.ccuVersion != CCU_VERSION_V1 && hcommContext.ccuVersion != CCU_VERSION_V2)) {
        return HCCL_E_PARA;
    }

    storage.instanceRanges.clear();
    storage.translatorRanges = hcommContextStorage.translatorRanges;
    storage.context = {};
    storage.context.header = {HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD,
        sizeof(HcommCcuRegisterContextPod), 0};
    storage.context.generation = hcommContext.generation;
    storage.context.deviceLogicId = hcommContext.deviceLogicId;
    storage.context.ccuVersion = hcommContext.ccuVersion;
    storage.context.dieNum = hcommContext.dieNum;
    storage.context.validDieMask = hcommContext.validDieMask;
    storage.context.instanceHandle = hcommContext.instanceHandle;
    storage.context.instanceType = hcommContext.instanceType;
    storage.context.controlOps = hcommContext.controlOps;

    for (uint32_t dieId = 0; dieId < HCOMM_CCU_MAX_DIE_NUM; ++dieId) {
        if ((storage.context.validDieMask & (1U << dieId)) == 0) {
            continue;
        }
        HcclResult ret = AddDefaultRangesForDie(hcommContextStorage, storage, dieId);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }
    }
    HcclResult ret = AddInstructionRangesFromHcommContext(hcommContextStorage, storage);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    storage.context.instanceResources = {
        storage.instanceRanges.data(), static_cast<uint32_t>(storage.instanceRanges.size()), 0};
    storage.context.translatorResources = {
        storage.translatorRanges.data(), static_cast<uint32_t>(storage.translatorRanges.size()), 0};
    return HCCL_SUCCESS;
}

} // namespace asccomm_examples

#endif // ASCCOMM_EXAMPLES_CCU_USER_RESOURCE_CONTEXT_H
