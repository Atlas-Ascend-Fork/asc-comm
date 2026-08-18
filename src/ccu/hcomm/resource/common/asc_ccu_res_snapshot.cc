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
        case HCOMM_CCU_RES_INSTRUCTION: return &repository.instruction[dieId];
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

bool IsControlOpsValid(const HcommCcuControlOpsPod &ops)
{
    return ops.header.version == HCOMM_CCU_CONTROL_ABI_VERSION &&
        ops.header.magicWord == HCOMM_CCU_CONTROL_OPS_MAGIC_WORD &&
        ops.header.size == sizeof(HcommCcuControlOpsPod) && ops.header.reserved == 0 &&
        ops.channelQuery != nullptr && ops.loadInstruction != nullptr && ops.missionContextQuery != nullptr &&
        ops.loopContextQuery != nullptr && ops.ckeQuery != nullptr && ops.xnQuery != nullptr &&
        ops.gsaQuery != nullptr && ops.reserved == 0;
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

CcuResult ValidateAndAppend(const HcommCcuResRangeViewPod &view,
    uint32_t validDieMask, std::vector<HcommCcuResRangePod> &allRanges, AscCcuResRepository &repository)
{
    if (!IsViewShapeValid(view)) {
        return CcuResult::CCU_E_PARA;
    }
    for (uint32_t index = 0; index < view.count; ++index) {
        const HcommCcuResRangePod &range = view.ranges[index];
        if (range.dieId >= HCOMM_CCU_MAX_DIE_NUM || (validDieMask & (1U << range.dieId)) == 0 || range.count == 0 ||
            static_cast<uint64_t>(range.startId) + range.count >
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1U) {
            return CcuResult::CCU_E_PARA;
        }
        if (range.resourceType > HCOMM_CCU_RES_INSTRUCTION) {
            return CcuResult::CCU_E_PARA;
        }
        for (const auto &existing : allRanges) {
            if (RangesOverlap(existing, range)) {
                return CcuResult::CCU_E_PARA;
            }
        }
        allRanges.push_back(range);
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
        !IsControlOpsValid(context.controlOps) || !IsViewShapeValid(context.instanceResources)) {
        return CcuResult::CCU_E_PARA;
    }
    if (generation_ != 0 && generation_ != context.generation) {
        return CcuResult::CCU_E_PARA;
    }

    AscCcuResRepository instanceSnapshot{};
    std::vector<HcommCcuResRangePod> allRanges;
    allRanges.reserve(context.instanceResources.count);

    CcuResult ret = ValidateAndAppend(context.instanceResources, context.validDieMask, allRanges, instanceSnapshot);
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }

    for (uint32_t dieId = 0; dieId < HCOMM_CCU_MAX_DIE_NUM; ++dieId) {
        const bool enabled = (context.validDieMask & (1U << dieId)) != 0;
        const HcommCcuDieMetadataPod &metadata = context.die[dieId];
        if (metadata.enabled > 1 || (metadata.enabled != 0) != enabled || metadata.reserved != 0 ||
            (!enabled && !IsDisabledDieMetadataValid(metadata))) {
            return CcuResult::CCU_E_PARA;
        }
    }

    AscCcuResRepository currentRepo = instanceSnapshot;
    generation_ = context.generation;
    deviceLogicId_ = context.deviceLogicId;
    ccuVersion_ = context.ccuVersion;
    validDieMask_ = context.validDieMask;
    controlOps_ = context.controlOps;
    initialRepo_ = std::move(instanceSnapshot);
    resRepo_ = std::move(currentRepo);
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

AscCcuResRepository &AscCcuResSnapshot::GetCcuResRepo() { return resRepo_; }

const HcommCcuDieMetadataPod *AscCcuResSnapshot::GetDieMetadata(uint32_t dieId) const
{
    return dieId < HCOMM_CCU_MAX_DIE_NUM && (validDieMask_ & (1U << dieId)) != 0 ? &dieMetadata_[dieId] : nullptr;
}

uint64_t AscCcuResSnapshot::GetGeneration() const { return generation_; }

int32_t AscCcuResSnapshot::GetDeviceLogicId() const { return deviceLogicId_; }

uint32_t AscCcuResSnapshot::GetCcuVersion() const { return ccuVersion_; }

uint32_t AscCcuResSnapshot::GetValidDieMask() const { return validDieMask_; }

const HcommCcuControlOpsPod &AscCcuResSnapshot::GetControlOps() const { return controlOps_; }

} // namespace hcomm
