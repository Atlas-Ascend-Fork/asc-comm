/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_res_pack.h"

#include <algorithm>

#include "ccu_log.h"

namespace hcomm {
namespace {

    std::vector<ResInfo> *SelectResource(CcuResRepository &repository, uint32_t type, uint32_t dieId)
    {
        switch (type) {
            case ASCCOMM_CCU_RES_LOOP:
                return &repository.loopEngine[dieId];
            case ASCCOMM_CCU_RES_BLOCK_LOOP:
                return &repository.blockLoopEngine[dieId];
            case ASCCOMM_CCU_RES_MS:
                return &repository.ms[dieId];
            case ASCCOMM_CCU_RES_BLOCK_MS:
                return &repository.blockMs[dieId];
            case ASCCOMM_CCU_RES_CKE:
                return &repository.cke[dieId];
            case ASCCOMM_CCU_RES_BLOCK_CKE:
                return &repository.blockCke[dieId];
            case ASCCOMM_CCU_RES_CONTINUOUS_XN:
                return &repository.continuousXn[dieId];
            case ASCCOMM_CCU_RES_XN:
                return &repository.xn[dieId];
            case ASCCOMM_CCU_RES_GSA:
                return &repository.gsa[dieId];
            case ASCCOMM_CCU_RES_MISSION:
                return &repository.mission.mission[dieId];
            default:
                return nullptr;
        }
    }

    uint64_t ResourceCount(const std::vector<ResInfo> &resources)
    {
        uint64_t count = 0;
        for (const auto &resource : resources)
            count += resource.num;
        return count;
    }

    CcuResult TakeResources(std::vector<ResInfo> &source, uint32_t count, std::vector<ResInfo> &destination)
    {
        if (ResourceCount(source) < count)
            return CCU_E_UNAVAIL;
        while (count != 0) {
            ResInfo &last = source.back();
            const uint32_t take = std::min(count, last.num);
            destination.emplace_back(last.startId + last.num - take, take);
            last.num -= take;
            count -= take;
            if (last.num == 0)
                source.pop_back();
        }
        std::reverse(destination.begin(), destination.end());
        return CCU_SUCCESS;
    }

} // namespace

CcuResult CcuResPack::Init(
    const AscCommCcuInstanceCreateParam &param, const AscCommCcuResourceRequest &translatorRequest)
{
    initialResRepo_ = {};
    translatorResRepo_ = {};
    initialResRepo_.mission.reqType = static_cast<MissionReqType>(param.missionRequestType);
    translatorResRepo_.mission.reqType = initialResRepo_.mission.reqType;
    for (uint32_t index = 0; index < param.resourceEntryCount; ++index) {
        const auto &entry = param.resourceEntries[index];
        if (entry.type >= ASCCOMM_CCU_RES_TYPE_COUNT || entry.dieId >= CCU_MAX_IODIE_NUM || entry.count == 0) {
            return CCU_E_PARA;
        }
        auto *resources = SelectResource(initialResRepo_, entry.type, entry.dieId);
        if (resources == nullptr)
            return CCU_E_INTERNAL;
        resources->emplace_back(entry.startId, entry.count);
    }

    for (uint32_t type = 0; type < ASCCOMM_CCU_RES_TYPE_COUNT; ++type) {
        for (uint32_t die = 0; die < CCU_MAX_IODIE_NUM; ++die) {
            auto *source = SelectResource(initialResRepo_, type, die);
            auto *destination = SelectResource(translatorResRepo_, type, die);
            if (source == nullptr || destination == nullptr)
                return CCU_E_INTERNAL;
            CCU_CHK_RET(TakeResources(*source, translatorRequest.count[type][die], *destination));
        }
    }
    return Reset();
}

CcuResult CcuResPack::Reset()
{
    resRepo_ = initialResRepo_;
    return CCU_SUCCESS;
}

const CcuResRepository &CcuResPack::GetTranslatorResRepo() const
{
    return translatorResRepo_;
}

CcuResRepository &CcuResPack::GetCcuResRepo()
{
    return resRepo_;
}

} // namespace hcomm
