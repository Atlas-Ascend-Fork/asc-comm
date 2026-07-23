/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_PROVIDER_INTERNAL_H
#define ASCCOMM_CCU_PROVIDER_INTERNAL_H

#include <memory>

#include "ccu_types.h"
#include "ccu_dataplane_abi.h"
#include "ccu_res_repo.h"
#include "hccl_types.h"

namespace hcomm {

class CcuInstance;

struct CcuChannelData {
    uint32_t dieId{0};
    uint32_t channelId{0};
    uint32_t localCkeId{0};
    uint32_t localXnId{0};
    uint32_t remoteCkeId{0};
    uint32_t remoteXnId{0};
    uint64_t remoteAddress{0};
    uint32_t remoteSize{0};
    uint32_t remoteTokenId{0};
    uint32_t remoteTokenValue{0};
};

CcuResult CcuReadChannelSnapshot(AscCommCcuChannelHandle channel, uint32_t index, CcuChannelData &data);
CcuResult CcuRetainHostServices(const AscCommCcuHostServicesV1 *services);
void CcuReleaseHostServices();
const AscCommCcuHostServicesV1 *CcuGetHostServices();
void CcuSetCurrentInstance(CcuInstance *instance);
CcuInstance *CcuGetCurrentInstance();

CcuResult CcuGetCurrentDevice(uint32_t &deviceLogicId);
CcuResult CcuGetDieEnableInfo(int32_t deviceLogicId, uint8_t dieId, bool &enabled);
CcuResult CcuQueryMemoryToken(uint64_t address, uint64_t size, uint64_t &tokenId, uint64_t &tokenValue);
CcuResult CcuQueryDeviceMemoryToken(uint32_t deviceLogicId, uint64_t size, uint64_t &tokenId, uint64_t &tokenValue);
CcuResult CcuQueryChannel(AscCommCcuChannelHandle channel, uint32_t index, CcuChannelData &data);
CcuResult CcuSubmitInstruction(
    uint32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId, const void *instructions, uint64_t size);
CcuResult CcuAllocResHandleByInsType(
    int32_t deviceLogicId, CcuInstanceType instanceType, CcuResHandle &resourceHandle);
CcuResult CcuCheckResource(
    int32_t deviceLogicId, CcuResHandle resourceHandle, CcuResRepository &repository);
HcclResult CcuReleaseResHandle(int32_t deviceLogicId, CcuResHandle resourceHandle);

class CcuDevMgrImp {
public:
    static HcclResult GetCcuResourceSpaceTokenInfo(
        int32_t deviceLogicId, uint8_t dieId, uint64_t &tokenId, uint64_t &tokenValue);
    static HcclResult GetLoopChannelId(
        int32_t deviceLogicId, uint8_t sourceDieId, uint8_t destinationDieId, uint32_t &channelId);
    static HcclResult AllocResHandle(int32_t deviceLogicId, const CcuResReq &request, CcuResHandle &resourceHandle);
    static HcclResult GetResource(
        int32_t deviceLogicId, CcuResHandle resourceHandle, CcuResRepository &repository);
    static HcclResult AllocIns(int32_t deviceLogicId, uint8_t dieId, uint32_t count, ResInfo &info);
    static HcclResult ReleaseIns(int32_t deviceLogicId, uint8_t dieId, const ResInfo &info);
    static HcclResult GetMissionKey(int32_t deviceLogicId, uint8_t dieId, uint32_t &missionKey);
    static HcclResult GetInstructionNum(int32_t deviceLogicId, uint8_t dieId, uint32_t &instructionCount);
    static HcclResult GetXnBaseAddr(uint32_t deviceLogicId, uint8_t dieId, uint64_t &address);
};

} // namespace hcomm

#endif
