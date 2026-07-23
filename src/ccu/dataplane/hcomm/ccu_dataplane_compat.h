/*
 * Compatibility glue for building migrated hcomm CCU data-plane sources
 * outside the original hcomm target.
 */
#ifndef ASCCOMM_CCU_DATAPLANE_COMPAT_H
#define ASCCOMM_CCU_DATAPLANE_COMPAT_H

#include <cstdint>
#include <string>

#include "exception_util.h"
#include "log.h"

using s8 = int8_t;
using u8 = uint8_t;
using s16 = int16_t;
using u16 = uint16_t;
using s32 = int32_t;
using u32 = uint32_t;
using s64 = int64_t;
using u64 = uint64_t;
using std::string;

namespace hcomm {
constexpr uint16_t CCU_RESOURCE_XN_PER_SIZE = 8;
constexpr uint32_t MAX_MODULE_DEVICE_NUM = 65;
}

#ifndef HCCL_RUN_WARNING
#define HCCL_RUN_WARNING(...) HCCL_WARNING(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_DEBUG
#define HCCL_CONFIG_DEBUG(config, ...) HCCL_DEBUG(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_INFO
#define HCCL_CONFIG_INFO(config, ...) HCCL_INFO(__VA_ARGS__)
#endif

#ifndef HCCL_ENTRY_INFO
#define HCCL_ENTRY_INFO(opEntry, ...) HCCL_INFO(__VA_ARGS__)
#endif

#endif // ASCCOMM_CCU_DATAPLANE_COMPAT_H
