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

namespace asc {
namespace {

bool take_contiguous(
    std::vector<HcommCcuResRangePod>& source, uint32_t count, std::vector<HcommCcuResRangePod>& destination)
{
    if (count == 0) {
        return true;
    }
    auto iter = std::find_if(
        source.begin(), source.end(), [count](const HcommCcuResRangePod& range) { return range.count >= count; });
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

uint32_t take_any(
    std::vector<HcommCcuResRangePod>& source, uint32_t count, uint32_t destination_type,
    std::vector<HcommCcuResRangePod>& destination)
{
    auto iter = source.begin();
    while (count > 0 && iter != source.end()) {
        const uint32_t take = std::min(count, iter->count);
        destination.push_back({destination_type, iter->dieId, iter->startId, take});
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

bool plan_resource_pair(
    std::vector<HcommCcuResRangePod>& normal_pool, std::vector<HcommCcuResRangePod>& block_pool,
    uint32_t normal_request, uint32_t block_request, uint32_t normal_type,
    std::vector<HcommCcuResRangePod>& normal_allocated, std::vector<HcommCcuResRangePod>& block_allocated)
{
    if (!take_contiguous(block_pool, block_request, block_allocated)) {
        return false;
    }

    uint32_t remaining_request = take_any(normal_pool, normal_request, normal_type, normal_allocated);
    remaining_request = take_any(block_pool, remaining_request, normal_type, normal_allocated);
    return remaining_request == 0;
}

} // namespace

CcuResult plan_kernel_resources(
    const asc_ccu_res_repository& available, const asc_ccu_res_request& request, asc_ccu_res_repository& remaining,
    asc_ccu_res_repository& allocated)
{
    asc_ccu_res_repository planned_remaining = available;
    asc_ccu_res_repository planned_allocated{};

    for (uint32_t die_id = 0; die_id < HCOMM_CCU_MAX_DIE_NUM; ++die_id) {
        const bool planned =
            plan_resource_pair(
                planned_remaining.loop_engine[die_id], planned_remaining.block_loop_engine[die_id],
                request.count[HCOMM_CCU_BATCH_RES_LOOP][die_id], request.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][die_id],
                HCOMM_CCU_BATCH_RES_LOOP, planned_allocated.loop_engine[die_id],
                planned_allocated.block_loop_engine[die_id]) &&
            plan_resource_pair(
                planned_remaining.ms[die_id], planned_remaining.block_ms[die_id],
                request.count[HCOMM_CCU_BATCH_RES_MS][die_id], request.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][die_id],
                HCOMM_CCU_BATCH_RES_MS, planned_allocated.ms[die_id], planned_allocated.block_ms[die_id]) &&
            plan_resource_pair(
                planned_remaining.cke[die_id], planned_remaining.block_cke[die_id],
                request.count[HCOMM_CCU_BATCH_RES_CKE][die_id], request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][die_id],
                HCOMM_CCU_BATCH_RES_CKE, planned_allocated.cke[die_id], planned_allocated.block_cke[die_id]) &&
            plan_resource_pair(
                planned_remaining.xn[die_id], planned_remaining.block_xn[die_id],
                request.count[HCOMM_CCU_BATCH_RES_XN][die_id], request.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][die_id],
                HCOMM_CCU_BATCH_RES_XN, planned_allocated.xn[die_id], planned_allocated.block_xn[die_id]) &&
            plan_resource_pair(
                planned_remaining.gsa[die_id], planned_remaining.block_gsa[die_id],
                request.count[HCOMM_CCU_BATCH_RES_GSA][die_id], request.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][die_id],
                HCOMM_CCU_BATCH_RES_GSA, planned_allocated.gsa[die_id], planned_allocated.block_gsa[die_id]);
        if (!planned ||
            take_any(
                planned_remaining.mission[die_id], request.count[HCOMM_CCU_BATCH_RES_MISSION][die_id],
                HCOMM_CCU_BATCH_RES_MISSION, planned_allocated.mission[die_id]) != 0 ||
            !take_contiguous(
                planned_remaining.instruction[die_id], request.instruction[die_id],
                planned_allocated.instruction[die_id])) {
            return CcuResult::CCU_E_UNAVAIL;
        }
    }

    remaining = std::move(planned_remaining);
    allocated = std::move(planned_allocated);
    return CcuResult::CCU_SUCCESS;
}

} // namespace asc
