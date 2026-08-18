/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_res_allocator.h"

#include <algorithm>
#include <utility>

namespace hcomm {
namespace {

enum class NormalAllocDirection {
    LOW_ID,
    HIGH_ID,
};

bool TakeContiguous(std::vector<HcommCcuResRangePod>& source, uint32_t count,
    std::vector<HcommCcuResRangePod>& destination)
{
    if (count == 0) {
        return true;
    }
    auto iter = std::find_if(source.begin(), source.end(), [count](const HcommCcuResRangePod& range) {
        return range.count >= count;
    });
    if (iter == source.end()) {
        return false;
    }

    destination.push_back({iter->resourceType, iter->dieId, iter->startId, count});
    if (iter->count == count) {
        source.erase(iter);
    } else {
        iter->startId += count;
        iter->count -= count;
    }
    return true;
}

uint32_t TakeAny(std::vector<HcommCcuResRangePod>& source, uint32_t count, uint32_t destinationType,
    std::vector<HcommCcuResRangePod>& destination)
{
    auto iter = source.begin();
    while (count > 0 && iter != source.end()) {
        const uint32_t take = std::min(count, iter->count);
        destination.push_back({destinationType, iter->dieId, iter->startId, take});
        count -= take;
        if (take == iter->count) {
            iter = source.erase(iter);
        } else {
            iter->startId += take;
            iter->count -= take;
        }
    }
    return count;
}

uint32_t TakeAnyFromHigh(std::vector<HcommCcuResRangePod>& source, uint32_t count, uint32_t destinationType,
    std::vector<HcommCcuResRangePod>& destination)
{
    while (count > 0 && !source.empty()) {
        auto iter = std::max_element(source.begin(), source.end(),
            [](const HcommCcuResRangePod& lhs, const HcommCcuResRangePod& rhs) {
                const uint64_t lhsEnd = static_cast<uint64_t>(lhs.startId) + lhs.count;
                const uint64_t rhsEnd = static_cast<uint64_t>(rhs.startId) + rhs.count;
                return lhsEnd < rhsEnd;
            });
        const uint32_t take = std::min(count, iter->count);
        const uint32_t startId = static_cast<uint32_t>(static_cast<uint64_t>(iter->startId) + iter->count - take);
        destination.push_back({destinationType, iter->dieId, startId, take});
        count -= take;
        iter->count -= take;
        if (iter->count == 0) {
            source.erase(iter);
        }
    }
    return count;
}

uint32_t TakeNormal(std::vector<HcommCcuResRangePod>& source, uint32_t count, uint32_t destinationType,
    NormalAllocDirection direction, std::vector<HcommCcuResRangePod>& destination)
{
    if (direction == NormalAllocDirection::HIGH_ID) {
        return TakeAnyFromHigh(source, count, destinationType, destination);
    }
    return TakeAny(source, count, destinationType, destination);
}

bool PlanResourcePair(
    std::vector<HcommCcuResRangePod>& normalPool, std::vector<HcommCcuResRangePod>& blockPool,
    uint32_t normalRequest, uint32_t blockRequest, uint32_t normalType,
    NormalAllocDirection normalDirection,
    std::vector<HcommCcuResRangePod>& normalAllocated, std::vector<HcommCcuResRangePod>& blockAllocated)
{
    if (!TakeContiguous(blockPool, blockRequest, blockAllocated)) {
        return false;
    }

    uint32_t remainingRequest = TakeNormal(normalPool, normalRequest, normalType, normalDirection, normalAllocated);
    remainingRequest = TakeNormal(blockPool, remainingRequest, normalType, normalDirection, normalAllocated);
    return remainingRequest == 0;
}

CcuResult PlanResources(
    const AscCcuResRepository& available, const AscCcuResRequest& request, AscCcuResRepository& remaining,
    AscCcuResRepository& allocated, NormalAllocDirection normalDirection)
{
    AscCcuResRepository plannedRemaining = available;
    AscCcuResRepository plannedAllocated{};

    for (uint32_t dieId = 0; dieId < HCOMM_CCU_MAX_DIE_NUM; ++dieId) {
        const bool planned =
            PlanResourcePair(
                plannedRemaining.loopEngine[dieId], plannedRemaining.blockLoopEngine[dieId],
                request.count[HCOMM_CCU_BATCH_RES_LOOP][dieId],
                request.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][dieId], HCOMM_CCU_BATCH_RES_LOOP,
                normalDirection,
                plannedAllocated.loopEngine[dieId],
                plannedAllocated.blockLoopEngine[dieId]) &&
            PlanResourcePair(
                plannedRemaining.ms[dieId], plannedRemaining.blockMs[dieId],
                request.count[HCOMM_CCU_BATCH_RES_MS][dieId], request.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][dieId],
                HCOMM_CCU_BATCH_RES_MS, normalDirection, plannedAllocated.ms[dieId], plannedAllocated.blockMs[dieId]) &&
            PlanResourcePair(
                plannedRemaining.cke[dieId], plannedRemaining.blockCke[dieId],
                request.count[HCOMM_CCU_BATCH_RES_CKE][dieId], request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][dieId],
                HCOMM_CCU_BATCH_RES_CKE, normalDirection, plannedAllocated.cke[dieId],
                plannedAllocated.blockCke[dieId]) &&
            PlanResourcePair(
                plannedRemaining.xn[dieId], plannedRemaining.blockXn[dieId],
                request.count[HCOMM_CCU_BATCH_RES_XN][dieId], request.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][dieId],
                HCOMM_CCU_BATCH_RES_XN, normalDirection, plannedAllocated.xn[dieId], plannedAllocated.blockXn[dieId]) &&
            PlanResourcePair(
                plannedRemaining.gsa[dieId], plannedRemaining.blockGsa[dieId],
                request.count[HCOMM_CCU_BATCH_RES_GSA][dieId], request.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][dieId],
                HCOMM_CCU_BATCH_RES_GSA, normalDirection, plannedAllocated.gsa[dieId],
                plannedAllocated.blockGsa[dieId]);
        if (!planned || TakeNormal(
                            plannedRemaining.mission[dieId], request.count[HCOMM_CCU_BATCH_RES_MISSION][dieId],
                            HCOMM_CCU_BATCH_RES_MISSION, normalDirection, plannedAllocated.mission[dieId]) != 0 ||
            !TakeContiguous(
                plannedRemaining.instruction[dieId], request.instruction[dieId], plannedAllocated.instruction[dieId])) {
            return CcuResult::CCU_E_UNAVAIL;
        }
    }

    remaining = std::move(plannedRemaining);
    allocated = std::move(plannedAllocated);
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult PlanKernelResources(
    const AscCcuResRepository& available, const AscCcuResRequest& request, AscCcuResRepository& remaining,
    AscCcuResRepository& allocated)
{
    return PlanResources(available, request, remaining, allocated, NormalAllocDirection::LOW_ID);
}

CcuResult PlanTranslatorResources(
    const AscCcuResRepository& available, const AscCcuResRequest& request, AscCcuResRepository& remaining,
    AscCcuResRepository& allocated)
{
    return PlanResources(available, request, remaining, allocated, NormalAllocDirection::HIGH_ID);
}

} // namespace hcomm
