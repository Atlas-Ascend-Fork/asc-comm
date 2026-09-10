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
 * \file hcomm_log.h
 * \brief Hcomm debug-only kernel log macros
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_LOG_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_LOG_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_LOG_H

#if defined(ASCENDC_DEBUG)
#define HCOMM_DEBUG_FUNC(func, ...) \
    do {                            \
        func(__VA_ARGS__);          \
    } while (0)
// Reserved for debug-only checks where violating the condition must stop execution.
#define HCOMM_DEBUG_TRAP_IF(condition, fmt, ...) \
    do {                                         \
        if (condition) {                         \
            AscendC::PRINTF(fmt, ##__VA_ARGS__); \
            AscendC::Trap();                     \
        }                                        \
    } while (0)
#define HCOMM_KERNEL_LOG(level, fmt, ...) KERNEL_LOG(level, fmt, ##__VA_ARGS__)
#else
#define HCOMM_DEBUG_FUNC(func, ...) ((void)0)
#define HCOMM_DEBUG_TRAP_IF(condition, fmt, ...) ((void)0)
#define HCOMM_KERNEL_LOG(level, fmt, ...) HCOMM_KERNEL_LOG_##level(fmt, ##__VA_ARGS__)
#define HCOMM_KERNEL_LOG_KERNEL_INFO(fmt, ...) ((void)0)
#define HCOMM_KERNEL_LOG_KERNEL_ERROR(fmt, ...) KERNEL_LOG(KERNEL_ERROR, fmt, ##__VA_ARGS__)
#endif

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_LOG_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_LOG_H
#endif
