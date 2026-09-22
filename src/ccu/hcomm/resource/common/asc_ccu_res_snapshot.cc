/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/common/asc_ccu_res_snapshot.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace asc {
namespace {

std::vector<HcommCcuResRangePod>* get_ranges(
    asc_ccu_res_repository& repository, uint32_t resource_type, uint32_t die_id)
{
    switch (resource_type) {
        case HCOMM_CCU_BATCH_RES_LOOP:
            return &repository.loop_engine[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_LOOP:
            return &repository.block_loop_engine[die_id];
        case HCOMM_CCU_BATCH_RES_MS:
            return &repository.ms[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_MS:
            return &repository.block_ms[die_id];
        case HCOMM_CCU_BATCH_RES_CKE:
            return &repository.cke[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_CKE:
            return &repository.block_cke[die_id];
        case HCOMM_CCU_BATCH_RES_XN:
            return &repository.xn[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_XN:
            return &repository.block_xn[die_id];
        case HCOMM_CCU_BATCH_RES_GSA:
            return &repository.gsa[die_id];
        case HCOMM_CCU_BATCH_RES_BLOCK_GSA:
            return &repository.block_gsa[die_id];
        case HCOMM_CCU_BATCH_RES_MISSION:
            return &repository.mission[die_id];
        case HCOMM_CCU_RES_INSTRUCTION:
            return &repository.instruction[die_id];
        default:
            return nullptr;
    }
}

bool is_header_valid(const HcommCcuRegisterContextPod& context)
{
    return context.header.version == HCOMM_CCU_RES_ABI_VERSION &&
           context.header.magicWord == HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD &&
           context.header.size == sizeof(HcommCcuRegisterContextPod) && context.header.reserved == 0;
}

bool is_view_shape_valid(const HcommCcuResRangeViewPod& view)
{
    return view.reserved == 0 && (view.count == 0 || view.ranges != nullptr);
}

bool is_control_ops_valid(const HcommCcuControlOpsPod& ops)
{
    return ops.header.version == HCOMM_CCU_CONTROL_ABI_VERSION &&
           ops.header.magicWord == HCOMM_CCU_CONTROL_OPS_MAGIC_WORD &&
           ops.header.size == sizeof(HcommCcuControlOpsPod) && ops.header.reserved == 0 &&
           ops.channelQuery != nullptr && ops.loadInstruction != nullptr && ops.missionContextQuery != nullptr &&
           ops.loopContextQuery != nullptr && ops.ckeQuery != nullptr && ops.xnQuery != nullptr &&
           ops.gsaQuery != nullptr && ops.reserved == 0;
}

bool is_disabled_die_metadata_valid(const HcommCcuDieMetadataPod& metadata)
{
    return metadata.enabled == 0 && metadata.missionKey == 0 && metadata.innerDieLoopChannelId == 0 &&
           metadata.interDieLoopChannelId == 0 && metadata.xnBaseAddr == 0 && metadata.resourceSpaceTokenId == 0 &&
           metadata.resourceSpaceTokenValue == 0 && metadata.instructionCapacity == 0 && metadata.reserved == 0;
}

bool ranges_overlap(const HcommCcuResRangePod& left, const HcommCcuResRangePod& right)
{
    if (left.resourceType != right.resourceType || left.dieId != right.dieId) {
        return false;
    }
    const uint64_t left_end = static_cast<uint64_t>(left.startId) + left.count;
    const uint64_t right_end = static_cast<uint64_t>(right.startId) + right.count;
    return static_cast<uint64_t>(left.startId) < right_end && static_cast<uint64_t>(right.startId) < left_end;
}

CcuResult validate_and_append(
    const HcommCcuResRangeViewPod& view, uint32_t valid_die_mask, std::vector<HcommCcuResRangePod>& all_ranges,
    asc_ccu_res_repository& repository)
{
    if (!is_view_shape_valid(view)) {
        return CcuResult::CCU_E_PARA;
    }
    for (uint32_t index = 0; index < view.count; ++index) {
        const HcommCcuResRangePod& range = view.ranges[index];
        if (range.dieId >= HCOMM_CCU_MAX_DIE_NUM || (valid_die_mask & (1U << range.dieId)) == 0 || range.count == 0 ||
            static_cast<uint64_t>(range.startId) + range.count >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U) {
            return CcuResult::CCU_E_PARA;
        }
        if (range.resourceType > HCOMM_CCU_RES_INSTRUCTION) {
            return CcuResult::CCU_E_PARA;
        }
        for (const auto& existing : all_ranges) {
            if (ranges_overlap(existing, range)) {
                return CcuResult::CCU_E_PARA;
            }
        }
        all_ranges.push_back(range);
        auto* destination = get_ranges(repository, range.resourceType, range.dieId);
        if (destination == nullptr) {
            return CcuResult::CCU_E_PARA;
        }
        destination->push_back(range);
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult asc_ccu_res_snapshot::load(const HcommCcuRegisterContextPod& context)
{
    if (!is_header_valid(context) || context.generation == 0 || context.deviceLogicId < 0 || context.ccuVersion > 1 ||
        context.dieNum != HCOMM_CCU_MAX_DIE_NUM || context.validDieMask == 0 ||
        (context.validDieMask & ~((1U << HCOMM_CCU_MAX_DIE_NUM) - 1U)) != 0 || context.reserved[0] != 0 ||
        context.reserved[1] != 0 || !is_control_ops_valid(context.controlOps) ||
        !is_view_shape_valid(context.instanceResources)) {
        return CcuResult::CCU_E_PARA;
    }
    if (generation_ != 0 && generation_ != context.generation) {
        return CcuResult::CCU_E_PARA;
    }

    asc_ccu_res_repository instance_snapshot{};
    std::vector<HcommCcuResRangePod> all_ranges;
    all_ranges.reserve(context.instanceResources.count);

    CcuResult ret = validate_and_append(context.instanceResources, context.validDieMask, all_ranges, instance_snapshot);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }

    for (uint32_t die_id = 0; die_id < HCOMM_CCU_MAX_DIE_NUM; ++die_id) {
        const bool enabled = (context.validDieMask & (1U << die_id)) != 0;
        const HcommCcuDieMetadataPod& metadata = context.die[die_id];
        if (metadata.enabled > 1 || (metadata.enabled != 0) != enabled || metadata.reserved != 0 ||
            (!enabled && !is_disabled_die_metadata_valid(metadata))) {
            return CcuResult::CCU_E_PARA;
        }
    }

    asc_ccu_res_repository current_repo = instance_snapshot;
    generation_ = context.generation;
    device_logic_id_ = context.deviceLogicId;
    ccu_version_ = context.ccuVersion;
    valid_die_mask_ = context.validDieMask;
    control_ops_ = context.controlOps;
    initial_repo_ = std::move(instance_snapshot);
    res_repo_ = std::move(current_repo);
    die_metadata_[0] = context.die[0];
    die_metadata_[1] = context.die[1];
    return CcuResult::CCU_SUCCESS;
}

CcuResult asc_ccu_res_snapshot::reset()
{
    if (generation_ == 0) {
        return CcuResult::CCU_E_UNAVAIL;
    }
    res_repo_ = initial_repo_;
    return CcuResult::CCU_SUCCESS;
}

asc_ccu_res_repository& asc_ccu_res_snapshot::get_ccu_res_repo() { return res_repo_; }

const HcommCcuDieMetadataPod* asc_ccu_res_snapshot::get_die_metadata(uint32_t die_id) const
{
    return die_id < HCOMM_CCU_MAX_DIE_NUM && (valid_die_mask_ & (1U << die_id)) != 0 ? &die_metadata_[die_id] : nullptr;
}

uint64_t asc_ccu_res_snapshot::get_generation() const { return generation_; }

int32_t asc_ccu_res_snapshot::get_device_logic_id() const { return device_logic_id_; }

uint32_t asc_ccu_res_snapshot::get_ccu_version() const { return ccu_version_; }

uint32_t asc_ccu_res_snapshot::get_valid_die_mask() const { return valid_die_mask_; }

const HcommCcuControlOpsPod& asc_ccu_res_snapshot::get_control_ops() const { return control_ops_; }

} // namespace asc
