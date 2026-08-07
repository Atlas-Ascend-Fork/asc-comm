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

namespace hcomm {
namespace {

std::vector<HcommCcuResRangePod> *GetRanges(
    AscCcuResRepository &repository, uint32_t resourceType, uint32_t dieId)
{
    switch (resourceType) {
        case HCOMM_CCU_BATCH_RES_LOOP: return &repository.loopEngine[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_LOOP: return &repository.blockLoopEngine[dieId];
        case HCOMM_CCU_BATCH_RES_MS: return &repository.ms[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_MS: return &repository.blockMs[dieId];
        case HCOMM_CCU_BATCH_RES_CKE: return &repository.cke[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_CKE: return &repository.blockCke[dieId];
        case HCOMM_CCU_BATCH_RES_XN: return &repository.xn[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_XN: return &repository.blockXn[dieId];
        case HCOMM_CCU_BATCH_RES_GSA: return &repository.gsa[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_GSA: return &repository.blockGsa[dieId];
        case HCOMM_CCU_BATCH_RES_MISSION: return &repository.mission[dieId];
        default: return nullptr;
    }
}

bool IsHeaderValid(const HcommCcuRegisterContextPod &context)
{
    return context.header.version == HCOMM_CCU_RES_ABI_VERSION &&
        context.header.magicWord == HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD &&
        context.header.size == sizeof(HcommCcuRegisterContextPod) && context.header.reserved == 0;
}

bool IsViewShapeValid(const HcommCcuResRangeViewPod &view)
{
    return view.reserved == 0 && (view.count == 0 || view.ranges != nullptr);
}

bool IsDisabledDieMetadataValid(const HcommCcuDieMetadataPod &metadata)
{
    return metadata.enabled == 0 && metadata.missionKey == 0 && metadata.innerDieLoopChannelId == 0 &&
        metadata.interDieLoopChannelId == 0 && metadata.xnBaseAddr == 0 && metadata.resourceSpaceTokenId == 0 &&
        metadata.resourceSpaceTokenValue == 0 && metadata.instructionCapacity == 0 && metadata.reserved == 0;
}

bool RangesOverlap(const HcommCcuResRangePod &left, const HcommCcuResRangePod &right)
{
    if (left.resourceType != right.resourceType || left.dieId != right.dieId) {
        return false;
    }
    const uint64_t leftEnd = static_cast<uint64_t>(left.startId) + left.count;
    const uint64_t rightEnd = static_cast<uint64_t>(right.startId) + right.count;
    return static_cast<uint64_t>(left.startId) < rightEnd && static_cast<uint64_t>(right.startId) < leftEnd;
}

CcuResult ValidateAndAppend(const HcommCcuResRangeViewPod &view, bool instruction,
    uint32_t validDieMask, std::vector<HcommCcuResRangePod> &allRanges, AscCcuResRepository &repository,
    std::array<HcommCcuResRangePod, HCOMM_CCU_MAX_DIE_NUM> *instructionPools)
{
    for (uint32_t index = 0; index < view.count; ++index) {
        const HcommCcuResRangePod &range = view.ranges[index];
        if (range.dieId >= HCOMM_CCU_MAX_DIE_NUM || (validDieMask & (1U << range.dieId)) == 0 || range.count == 0 ||
            static_cast<uint64_t>(range.startId) + range.count >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U) {
            return CcuResult::CCU_E_PARA;
        }
        if ((instruction && range.resourceType != HCOMM_CCU_RES_INSTRUCTION) ||
            (!instruction && range.resourceType >= HCOMM_CCU_BATCH_RES_TYPE_COUNT)) {
            return CcuResult::CCU_E_PARA;
        }
        for (const auto &existing : allRanges) {
            if (RangesOverlap(existing, range)) {
                return CcuResult::CCU_E_PARA;
            }
        }
        allRanges.push_back(range);
        if (instruction) {
            HcommCcuResRangePod &pool = (*instructionPools)[range.dieId];
            if (pool.count != 0) {
                return CcuResult::CCU_E_PARA;
            }
            pool = range;
            continue;
        }
        auto *destination = GetRanges(repository, range.resourceType, range.dieId);
        if (destination == nullptr) {
            return CcuResult::CCU_E_PARA;
        }
        destination->push_back(range);
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult AscCcuResSnapshot::Load(const HcommCcuRegisterContextPod &context)
{
    if (!IsHeaderValid(context) || context.generation == 0 || context.deviceLogicId < 0 || context.ccuVersion > 1 ||
        context.dieNum != HCOMM_CCU_MAX_DIE_NUM ||
        context.validDieMask == 0 || (context.validDieMask & ~((1U << HCOMM_CCU_MAX_DIE_NUM) - 1U)) != 0 ||
        context.reserved[0] != 0 || context.reserved[1] != 0 ||
        !IsViewShapeValid(context.instanceResources) || !IsViewShapeValid(context.translatorResources) ||
        !IsViewShapeValid(context.instructionResources)) {
        return CcuResult::CCU_E_PARA;
    }
    if (generation_ != 0 && generation_ != context.generation) {
        return CcuResult::CCU_E_PARA;
    }

    AscCcuResRepository instanceSnapshot{};
    AscCcuResRepository translatorSnapshot{};
    std::array<HcommCcuResRangePod, HCOMM_CCU_MAX_DIE_NUM> instructionPools{};
    std::vector<HcommCcuResRangePod> allRanges;
    allRanges.reserve(context.instanceResources.count + context.translatorResources.count +
        context.instructionResources.count);

    CcuResult ret = ValidateAndAppend(context.instanceResources, false, context.validDieMask, allRanges,
        instanceSnapshot, nullptr);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }
    ret = ValidateAndAppend(context.translatorResources, false, context.validDieMask, allRanges,
        translatorSnapshot, nullptr);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }
    ret = ValidateAndAppend(context.instructionResources, true, context.validDieMask, allRanges,
        translatorSnapshot, &instructionPools);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }

    for (uint32_t dieId = 0; dieId < HCOMM_CCU_MAX_DIE_NUM; ++dieId) {
        const bool enabled = (context.validDieMask & (1U << dieId)) != 0;
        const HcommCcuDieMetadataPod &metadata = context.die[dieId];
        if (metadata.enabled > 1 || (metadata.enabled != 0) != enabled || metadata.reserved != 0 ||
            (!enabled && !IsDisabledDieMetadataValid(metadata)) ||
            (enabled && (instructionPools[dieId].count == 0 ||
                instructionPools[dieId].count != metadata.instructionCapacity))) {
            return CcuResult::CCU_E_PARA;
        }
    }

    AscCcuResRepository currentRepo = instanceSnapshot;
    generation_ = context.generation;
    deviceLogicId_ = context.deviceLogicId;
    ccuVersion_ = context.ccuVersion;
    validDieMask_ = context.validDieMask;
    initialRepo_ = std::move(instanceSnapshot);
    resRepo_ = std::move(currentRepo);
    translatorRepo_ = std::move(translatorSnapshot);
    instructionPools_ = instructionPools;
    dieMetadata_[0] = context.die[0];
    dieMetadata_[1] = context.die[1];
    return CcuResult::CCU_SUCCESS;
}

CcuResult AscCcuResSnapshot::Reset()
{
    if (generation_ == 0) {
        return CcuResult::CCU_E_UNAVAIL;
    }
    resRepo_ = initialRepo_;
    return CcuResult::CCU_SUCCESS;
}

CcuResult AscCcuResSnapshot::AllocateInstruction(uint32_t dieId, uint32_t count, HcommCcuResRangePod &range)
{
    if (dieId >= HCOMM_CCU_MAX_DIE_NUM || count == 0 || (validDieMask_ & (1U << dieId)) == 0) {
        return CcuResult::CCU_E_PARA;
    }
    const HcommCcuResRangePod &pool = instructionPools_[dieId];
    if (instructionUsed_[dieId] > pool.count || count > pool.count - instructionUsed_[dieId]) {
        return CcuResult::CCU_E_UNAVAIL;
    }
    range = {HCOMM_CCU_RES_INSTRUCTION, dieId, pool.startId + instructionUsed_[dieId], count};
    instructionUsed_[dieId] += count;
    return CcuResult::CCU_SUCCESS;
}

AscCcuResRepository &AscCcuResSnapshot::GetCcuResRepo() { return resRepo_; }

const AscCcuResRepository &AscCcuResSnapshot::GetTranslatorResRepo() const { return translatorRepo_; }

const HcommCcuDieMetadataPod *AscCcuResSnapshot::GetDieMetadata(uint32_t dieId) const
{
    return dieId < HCOMM_CCU_MAX_DIE_NUM && (validDieMask_ & (1U << dieId)) != 0 ? &dieMetadata_[dieId] : nullptr;
}

uint64_t AscCcuResSnapshot::GetGeneration() const { return generation_; }

int32_t AscCcuResSnapshot::GetDeviceLogicId() const { return deviceLogicId_; }

uint32_t AscCcuResSnapshot::GetCcuVersion() const { return ccuVersion_; }

uint32_t AscCcuResSnapshot::GetValidDieMask() const { return validDieMask_; }

} // namespace hcomm
