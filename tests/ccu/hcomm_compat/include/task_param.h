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

constexpr uint16_t ccu_max_channel_num = 16;
constexpr uint16_t invalid_value_channelid = 0xFFFF;
constexpr uint32_t INVALID_VALUE_RANKID = UINT32_MAX;
constexpr uint64_t invalid_value_notifyid = UINT64_MAX;

struct ccu_profiling_info {
    std::string name;
    uint8_t type_;
    uint8_t dieId;
    uint8_t missionId;
    uint16_t instrId;
    uint8_t reduceOpType;
    uint8_t inputDataType;
    uint8_t outputDataType;
    uint64_t dataSize;
    uint32_t ckeId;
    uint32_t mask_;
    uint16_t channelId[ccu_max_channel_num];
    uint32_t remoteRankId[ccu_max_channel_num];
    uint64_t channelHandle[ccu_max_channel_num];

    ccu_profiling_info()
        : type_(0),
          dieId(0),
          missionId(0),
          instrId(0),
          reduceOpType(0),
          inputDataType(0),
          outputDataType(0),
          dataSize(0),
          ckeId(0),
          mask_(0)
    {
        (void)memset_s(channelId, sizeof(channelId), 0xFF, sizeof(channelId));
        for (uint32_t index = 0; index < ccu_max_channel_num; ++index) {
            remoteRankId[index] = INVALID_VALUE_RANKID;
            channelHandle[index] = invalid_value_notifyid;
        }
    }
};

} // namespace Hccl

#endif
