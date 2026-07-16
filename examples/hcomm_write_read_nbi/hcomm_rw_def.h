/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_RW_DEF_H
#define HCOMM_RW_DEF_H

#include <cstdint>

#include "adv_api/hcomm/hcomm.h"
#include "hccl/hccl.h"
// CommProtocol、COMM_PROTOCOL_UBC_CTP
#include "hccl/hccl_comm.h"


// 单次通信数据量（字节），需32字节对齐
constexpr uint32_t DATA_SIZE = 256U;
// 每张卡的通信窗口大小：4段DATA_SIZE（seg0/seg1/seg2/expected）< COMM_BUF_SIZE
constexpr uint64_t COMM_BUF_SIZE = 4096U;
// 通信卡数：样例简化为2卡点对点，可扩展为星形拓扑支持N卡
constexpr uint32_t NRANKS = 2U;
// Host侧建链协议需与Kernel侧Hcomm模板协议保持一致
constexpr CommProtocol TARGET_COMM_PROTOCOL = COMM_PROTOCOL_UBC_CTP;
// 最少的卡数
constexpr uint16_t MIN_RANKS = 2U;
// UBC_CTP通道需要的notify资源数量，需在HcclChannelAcquire前写入channelDesc
// constexpr uint32_t CHANNEL_NOTIFY_NUM = 3U;
// Hcomm工作空间大小下限，小于此值Init返回失败
constexpr uint32_t HCOMM_WORKSPACE_SIZE = 512U;

namespace HcommExample {

// Kernel与Host共享的通信上下文，存放在GM上
// Host侧构造后通过aclrtMemcpy下发到各卡GM，Kernel侧Init时从GM读取
struct CommContext {
    uint64_t channelHandle; // 本rank到对端的通道句柄
    uint64_t localBufferAddr; // 本rank的通信buffer基址（4段：seg0=本地pattern, seg1=WriteNbi目的, seg2=ReadNbi目的,
                              // seg3=对端pattern期望值）
    uint64_t remoteBufferAddr; // 对端的通信buffer基址
    uint32_t rankId;           // 本rank编号
    uint32_t worldSize;        // 通信域rank总数
    // 校验结果：kernel写入，host读回。0=通过，非0=失败
    uint32_t testResult;
    uint32_t mismatchIndex;
    uint32_t actualValue;
    uint32_t expectedValue;
};

} // namespace HcommExample

#endif // HCOMM_RW_DEF_H