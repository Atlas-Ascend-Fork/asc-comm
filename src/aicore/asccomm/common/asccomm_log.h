/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file asccomm_log.h
 * \brief Hcomm debug-only kernel log macros
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_LOG_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_LOG_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_LOG_H

#if defined(ASCENDC_DEBUG)
#define ASCCOMM_DEBUG_FUNC(func, ...) \
    do {                              \
        func(__VA_ARGS__);            \
    } while (0)
// Reserved for debug-only checks where violating the condition must stop execution.
#define ASCCOMM_DEBUG_TRAP_IF(condition, fmt, ...) \
    do {                                           \
        if (condition) {                           \
            AscendC::PRINTF(fmt, ##__VA_ARGS__);   \
            AscendC::Trap();                       \
        }                                          \
    } while (0)
#define ASCCOMM_KERNEL_LOG(level, fmt, ...)  \
    do {                                     \
        AscendC::PRINTF(fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define ASCCOMM_DEBUG_FUNC(func, ...) ((void)0)
#define ASCCOMM_DEBUG_TRAP_IF(condition, fmt, ...) ((void)0)
#define ASCCOMM_KERNEL_LOG(level, fmt, ...) ASCCOMM_KERNEL_LOG_##level(fmt, ##__VA_ARGS__)
#define ASCCOMM_KERNEL_LOG_KERNEL_INFO(fmt, ...) ((void)0)
#define ASCCOMM_KERNEL_LOG_KERNEL_WARN(fmt, ...) KERNEL_LOG(KERNEL_WARN, fmt, ##__VA_ARGS__)
#define ASCCOMM_KERNEL_LOG_KERNEL_ERROR(fmt, ...) KERNEL_LOG(KERNEL_ERROR, fmt, ##__VA_ARGS__)
#endif

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_LOG_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_LOG_H
#endif
