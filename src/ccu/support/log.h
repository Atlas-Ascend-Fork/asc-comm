/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 */
#ifndef ASCCOMM_CCU_LOG_SUPPORT_H
#define ASCCOMM_CCU_LOG_SUPPORT_H

#include <cstdio>
#include <securec.h>

#include "hccl_types.h"

#define LIKELY(x) (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))

#define HCCL_LOG_DEBUG 0
#define HCCL_LOG_INFO 1
#define HCCL_LOG_WARN 2
#define HCCL_LOG_ERROR 3
#define HCCL_LOG_OPLOG 6
#define HCCL_LOG_RUN_INFO 255

namespace Hccl {
inline int HcclCheckLogLevel(int) { return 1; }
}

#define ASCCOMM_CCU_LOG(level, format, ...) \
    do { std::fprintf(stderr, "[asccomm-ccu][%s] " format "\n", level, ##__VA_ARGS__); } while (0)
#define HCCL_DEBUG(...) ASCCOMM_CCU_LOG("DEBUG", __VA_ARGS__)
#define HCCL_INFO(...) ASCCOMM_CCU_LOG("INFO", __VA_ARGS__)
#define HCCL_WARNING(...) ASCCOMM_CCU_LOG("WARN", __VA_ARGS__)
#define HCCL_RUN_WARNING(...) ASCCOMM_CCU_LOG("WARN", __VA_ARGS__)
#define HCCL_ERROR(...) ASCCOMM_CCU_LOG("ERROR", __VA_ARGS__)
#define HCCL_RUN_INFO(...) ASCCOMM_CCU_LOG("RUN", __VA_ARGS__)
#define HCCL_ERROR_CODE(error) (static_cast<unsigned long long>(error))

#define CHK_PRT_RET(result, logStatement, returnCode) \
    do { if (UNLIKELY(result)) { logStatement; return returnCode; } } while (0)
#define CHK_RET(call) \
    do { const HcclResult result__ = static_cast<HcclResult>(call); \
        if (UNLIKELY(result__ != HCCL_SUCCESS)) return result__; } while (0)
#define CHK_PTR_NULL(pointer) \
    do { if (UNLIKELY((pointer) == nullptr)) return HCCL_E_PTR; } while (0)
#define CHK_SAFETY_FUNC_RET(call) \
    do { if (UNLIKELY((call) != 0)) return HCCL_E_INTERNAL; } while (0)

#endif
