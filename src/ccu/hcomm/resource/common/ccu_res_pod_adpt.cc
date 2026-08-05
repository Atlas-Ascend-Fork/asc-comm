/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/common/ccu_res_pod_adpt.h"

#include <utility>
#include <vector>

namespace hcomm {
namespace {

HcommCcuResRequestPod NormalizeRequest(const HcommCcuResRequestPod& request)
{
    HcommCcuResRequestPod pod = request;
    pod.header.version = HCOMM_CCU_RES_ABI_VERSION;
    pod.header.magicWord = HCOMM_CCU_RES_REQUEST_MAGIC_WORD;
    pod.header.size = sizeof(HcommCcuResRequestPod);
    pod.dieNum = HCOMM_CCU_MAX_DIE_NUM;
    return pod;
}

std::vector<HcommCcuResRangePod>* GetResourceRanges(
    AscCcuResRepository& repository, uint32_t resourceType, uint32_t dieId)
{
    switch (resourceType) {
        case HCOMM_CCU_BATCH_RES_LOOP:
            return &repository.loopEngine[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_LOOP:
            return &repository.blockLoopEngine[dieId];
        case HCOMM_CCU_BATCH_RES_MS:
            return &repository.ms[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_MS:
            return &repository.blockMs[dieId];
        case HCOMM_CCU_BATCH_RES_CKE:
            return &repository.cke[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_CKE:
            return &repository.blockCke[dieId];
        case HCOMM_CCU_BATCH_RES_XN:
            return &repository.xn[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_XN:
            return &repository.blockXn[dieId];
        case HCOMM_CCU_BATCH_RES_GSA:
            return &repository.gsa[dieId];
        case HCOMM_CCU_BATCH_RES_BLOCK_GSA:
            return &repository.blockGsa[dieId];
        case HCOMM_CCU_BATCH_RES_MISSION:
            return &repository.mission[dieId];
        default:
            return nullptr;
    }
}

CcuResult RebuildRepository(const std::vector<HcommCcuResRangePod>& ranges, AscCcuResRepository& repository)
{
    AscCcuResRepository snapshot{};
    for (const HcommCcuResRangePod& range : ranges) {
        if (range.dieId >= HCOMM_CCU_MAX_DIE_NUM) {
            return CcuResult::CCU_E_INTERNAL;
        }
        std::vector<HcommCcuResRangePod>* destination = GetResourceRanges(snapshot, range.resourceType, range.dieId);
        if (destination == nullptr) {
            return CcuResult::CCU_E_INTERNAL;
        }
        destination->push_back(range);
    }
    repository = std::move(snapshot);
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult CcuResPodAdapter::Alloc(
    int32_t deviceLogicId, const HcommCcuResRequestPod& request, HcommCcuResHandle& handle)
{
    HcommCcuResRequestPod pod = NormalizeRequest(request);
    return static_cast<CcuResult>(HcommCcuResAlloc(deviceLogicId, &pod, &handle));
}

CcuResult CcuResPodAdapter::AllocByInstanceType(int32_t deviceLogicId, uint32_t instanceType, HcommCcuResHandle& handle)
{
    return static_cast<CcuResult>(HcommCcuResAllocByInstanceType(deviceLogicId, instanceType, &handle));
}

CcuResult CcuResPodAdapter::GetRepository(HcommCcuResHandle handle, AscCcuResRepository& repository)
{
    uint32_t rangeNum = 0;
    CcuResult ret = static_cast<CcuResult>(HcommCcuResGetRanges(handle, nullptr, &rangeNum));
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }

    std::vector<HcommCcuResRangePod> ranges(rangeNum);
    if (rangeNum != 0) {
        ret = static_cast<CcuResult>(HcommCcuResGetRanges(handle, ranges.data(), &rangeNum));
        if (ret != CcuResult::CCU_SUCCESS) {
            return ret;
        }
        ranges.resize(rangeNum);
    }
    return RebuildRepository(ranges, repository);
}

CcuResult CcuResPodAdapter::Release(HcommCcuResHandle handle)
{
    return static_cast<CcuResult>(HcommCcuResRelease(handle));
}

CcuResult CcuResPodAdapter::AllocInstruction(
    int32_t deviceLogicId, uint32_t dieId, uint32_t count, HcommCcuResRangePod& range)
{
    HcommCcuResRangePod pod{};
    CcuResult ret = static_cast<CcuResult>(HcommCcuInstrResAlloc(deviceLogicId, dieId, count, &pod));
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }
    range = pod;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuResPodAdapter::ReleaseInstruction(int32_t deviceLogicId, uint32_t dieId, const HcommCcuResRangePod& range)
{
    const HcommCcuResRangePod pod{HCOMM_CCU_RES_INSTRUCTION, dieId, range.startId, range.count};
    return static_cast<CcuResult>(HcommCcuInstrResRelease(deviceLogicId, &pod));
}

} // namespace hcomm
