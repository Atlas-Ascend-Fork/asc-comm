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
 * \file ain_common.h
 * \brief ain common definitions
 */
#ifndef INCLUDE_COMM_API_AICORE_AIN_AIN_COMMON_H
#define INCLUDE_COMM_API_AICORE_AIN_AIN_COMMON_H

#ifndef AIN_DEVICE
/*!
 * @brief Force-inline qualifier for ain device-side member functions.
 *        All ain methods run on AICORE and are always inlined.
 */
#define AIN_DEVICE __attribute__((always_inline)) __aicore__ __inline__
#endif

namespace AscendC {

/*!
 * @brief Opaque handle to a symmetric communication window.
 */
using asccomm_window_handle = __gm__ void*;

/*!
 * @brief Opaque handle to a communication team.
 */
using asccomm_team_handle = __gm__ void*;

/*!
 * @brief Placeholder tag for "no remote action" in the put primitive.
 */
typedef struct {
} ain_remote_none;

/*!
 * @brief Placeholder tag indicating that no valid UB descriptor was supplied.
 *        Passing this type to put/get is rejected at compile time.
 */
typedef struct {
} ain_descriptor_ubuf_none;

/*!
 * @brief Describes the UB workspace handed to the underlying asccomm engine.
 */
typedef struct {
    __ubuf__ uint8_t* addr; /*!< Start address of the UB buffer. */
    uint32_t bytes;         /*!< Length of the UB buffer in bytes. */
    uint32_t event_id;       /*!< Event id reserved for synchronization with the comm engine. */
} ain_descriptor_ubuf;

/*!
 * @brief commit behavior for put/get tasks.
 */
enum ain_commit_flags {
    ain_commit_immed = 0,         /*!< Assemble the WQE and ring the doorbell immediately. */
    ain_commit_delayed = (1 << 0) /*!< Assemble the WQE only; submission is deferred until a subsequent task with
                                     ain_commit_immed rings the doorbell. */
};

/*!
 * @brief Memory ordering for signal read/wait operations.
 * @note ain_memory_order_relax only guarantees atomic read/write and threshold checking for the signal.
 *       It provides no memory ordering for non-signal data accesses before and after the signal operation.
 *       Use explicit barriers, pipe sync or other primitives to order ordinary data accesses.
 */
enum ain_memory_order { ain_memory_order_relax };

/*!
 * @brief signal action that atomically adds a custom value to a remote signal.
 */
typedef struct {
    asccomm_window_handle signal_window; /*!< Target signal window. */
    size_t signal_offset;            /*!< Target signal offset. */
    uint64_t value;                 /*!< Value to add. */
} ain_signal_add;

/*!
 * @brief signal action that atomically increments a remote signal by 1.
 */
typedef struct {
    asccomm_window_handle signal_window; /*!< Target signal window. */
    size_t signal_offset;            /*!< Target signal offset. */
} ain_signal_inc;

/*!
 * @brief Bitmask selecting the communication engine/protocol used by ain.
 */
enum ain_comm_engine_mask { ain_mask_default };

} // namespace AscendC

#endif // INCLUDE_COMM_API_AICORE_AIN_AIN_COMMON_H
