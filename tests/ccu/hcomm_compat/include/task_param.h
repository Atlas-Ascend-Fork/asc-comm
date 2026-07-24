/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_TASK_PARAM_H
#define ASCCOMM_CCU_UT_TASK_PARAM_H

#include <stdint.h>

#include <string>

#include <securec.h>

namespace Hccl {

constexpr uint16_t CCU_MAX_CHANNEL_NUM = 16;
constexpr uint16_t INVALID_VALUE_CHANNELID = 0xFFFF;
constexpr uint32_t INVALID_VALUE_RANKID = UINT32_MAX;
constexpr uint64_t INVALID_VALUE_NOTIFYID = UINT64_MAX;

struct CcuProfilingInfo {
    std::string name;
    uint8_t type;
    uint8_t dieId;
    uint8_t missionId;
    uint16_t instrId;
    uint8_t reduceOpType;
    uint8_t inputDataType;
    uint8_t outputDataType;
    uint64_t dataSize;
    uint32_t ckeId;
    uint32_t mask;
    uint16_t channelId[CCU_MAX_CHANNEL_NUM];
    uint32_t remoteRankId[CCU_MAX_CHANNEL_NUM];
    uint64_t channelHandle[CCU_MAX_CHANNEL_NUM];

    CcuProfilingInfo()
        : type(0), dieId(0), missionId(0), instrId(0), reduceOpType(0), inputDataType(0), outputDataType(0),
          dataSize(0), ckeId(0), mask(0)
    {
        (void)memset_s(channelId, sizeof(channelId), 0xFF, sizeof(channelId));
        for (uint32_t index = 0; index < CCU_MAX_CHANNEL_NUM; ++index) {
            remoteRankId[index] = INVALID_VALUE_RANKID;
            channelHandle[index] = INVALID_VALUE_NOTIFYID;
        }
    }
};

} // namespace Hccl

#endif
