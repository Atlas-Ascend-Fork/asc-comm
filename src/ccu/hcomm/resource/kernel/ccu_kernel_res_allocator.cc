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

bool TakeContiguous(std::vector<ResInfo>& source, uint32_t count, std::vector<ResInfo>& destination)
{
    if (count == 0) {
        return true;
    }
    auto iter =
        std::find_if(source.begin(), source.end(), [count](const ResInfo& range) { return range.num >= count; });
    if (iter == source.end()) {
        return false;
    }

    destination.emplace_back(iter->startId, count);
    if (iter->num == count) {
        source.erase(iter);
    } else {
        iter->startId += count;
        iter->num -= count;
    }
    return true;
}

uint32_t TakeAny(std::vector<ResInfo>& source, uint32_t count, std::vector<ResInfo>& destination)
{
    auto iter = source.begin();
    while (count > 0 && iter != source.end()) {
        const uint32_t take = std::min(count, iter->num);
        destination.emplace_back(iter->startId, take);
        count -= take;
        if (take == iter->num) {
            iter = source.erase(iter);
        } else {
            iter->startId += take;
            iter->num -= take;
        }
    }
    return count;
}

bool PlanResourcePair(
    std::vector<ResInfo>& normalPool, std::vector<ResInfo>& blockPool, uint32_t normalRequest, uint32_t blockRequest,
    std::vector<ResInfo>& normalAllocated, std::vector<ResInfo>& blockAllocated)
{
    if (!TakeContiguous(blockPool, blockRequest, blockAllocated)) {
        return false;
    }

    uint32_t remainingRequest = TakeAny(normalPool, normalRequest, normalAllocated);
    remainingRequest = TakeAny(blockPool, remainingRequest, normalAllocated);
    return remainingRequest == 0;
}

} // namespace

CcuResult PlanKernelResources(
    const CcuResRepository& available, const CcuResReq& request, CcuResRepository& remaining,
    CcuResRepository& allocated)
{
    CcuResRepository plannedRemaining = available;
    CcuResRepository plannedAllocated{};
    plannedAllocated.mission.reqType = request.missionReq.reqType;

    for (uint32_t dieId = 0; dieId < CCU_MAX_IODIE_NUM; ++dieId) {
        const bool planned =
            PlanResourcePair(
                plannedRemaining.loopEngine[dieId], plannedRemaining.blockLoopEngine[dieId],
                request.loopEngineReq[dieId], request.blockLoopEngineReq[dieId], plannedAllocated.loopEngine[dieId],
                plannedAllocated.blockLoopEngine[dieId]) &&
            PlanResourcePair(
                plannedRemaining.ms[dieId], plannedRemaining.blockMs[dieId], request.msReq[dieId],
                request.blockMsReq[dieId], plannedAllocated.ms[dieId], plannedAllocated.blockMs[dieId]) &&
            PlanResourcePair(
                plannedRemaining.cke[dieId], plannedRemaining.blockCke[dieId], request.ckeReq[dieId],
                request.blockCkeReq[dieId], plannedAllocated.cke[dieId], plannedAllocated.blockCke[dieId]) &&
            PlanResourcePair(
                plannedRemaining.xn[dieId], plannedRemaining.blockXn[dieId], request.xnReq[dieId],
                request.blockXnReq[dieId], plannedAllocated.xn[dieId], plannedAllocated.blockXn[dieId]) &&
            PlanResourcePair(
                plannedRemaining.gsa[dieId], plannedRemaining.blockGsa[dieId], request.gsaReq[dieId],
                request.blockGsaReq[dieId], plannedAllocated.gsa[dieId], plannedAllocated.blockGsa[dieId]);
        if (!planned || TakeAny(
                            plannedRemaining.mission.mission[dieId], request.missionReq.req[dieId],
                            plannedAllocated.mission.mission[dieId]) != 0) {
            return CcuResult::CCU_E_UNAVAIL;
        }
    }

    remaining = std::move(plannedRemaining);
    allocated = std::move(plannedAllocated);
    return CcuResult::CCU_SUCCESS;
}

} // namespace hcomm
