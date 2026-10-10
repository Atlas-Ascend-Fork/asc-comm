/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SEND_WITH_IMM_E2E_H
#define ASCCOMM_SEND_WITH_IMM_E2E_H

#include <cstdint>

namespace send_with_imm_e2e {

constexpr uint32_t kDefaultPayloadBytes = 64;
constexpr uint32_t kDefaultRecvSlots = 8;
constexpr uint16_t kDefaultTcpPort = 49152;

enum MessageKind : uint32_t {
    kEndpointDesc = 1,
    kMemoryDesc = 2,
    kReady = 3,
    kClientReady = 4,
    kResult = 5,
};

struct MessageHeader {
    uint32_t kind;
    uint32_t bytes;
};

struct TestConfig {
    uint32_t payloadBytes{kDefaultPayloadBytes};
    uint32_t immData{0x12345678u};
    uint32_t sendCount{1};
    uint32_t recvSlots{kDefaultRecvSlots};
    uint16_t listenPort{kDefaultTcpPort};
};

struct TestResult {
    uint32_t received{0};
    uint32_t errors{0};
    int32_t apiReturn{0};
};

} // namespace send_with_imm_e2e

#endif
