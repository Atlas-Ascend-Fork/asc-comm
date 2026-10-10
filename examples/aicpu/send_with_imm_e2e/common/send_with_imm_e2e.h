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
