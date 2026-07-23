/** Copyright (c) 2026 Huawei Technologies Co., Ltd. */
#ifndef ASCCOMM_CCU_PROFILING_RECORD_H
#define ASCCOMM_CCU_PROFILING_RECORD_H

#include <cstdint>
#include <cstring>
#include <string>

namespace Hccl {
constexpr uint32_t CCU_MAX_CHANNEL_NUM = 16;
struct CcuProfilingInfo {
    std::string name{};
    uint8_t type{0};
    uint8_t dieId{0};
    uint8_t missionId{0};
    uint16_t instrId{0};
    uint8_t reduceOpType{0};
    uint8_t inputDataType{0};
    uint8_t outputDataType{0};
    uint64_t dataSize{0};
    uint32_t ckeId{0};
    uint32_t mask{0};
    uint16_t channelId[CCU_MAX_CHANNEL_NUM]{};
    uint32_t remoteRankId[CCU_MAX_CHANNEL_NUM]{};
    uint64_t channelHandle[CCU_MAX_CHANNEL_NUM]{};
};
} // namespace Hccl
#endif
