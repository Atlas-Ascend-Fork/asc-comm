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
 * \brief Ain common definitions
 */
#ifndef INCLUDE_ADV_API_AIN_AIN_COMMON_H
#define INCLUDE_ADV_API_AIN_AIN_COMMON_H

#ifndef AIN_DEVICE
/*!
 * @brief Force-inline qualifier for Ain device-side member functions.
 *        All Ain methods run on AICORE and are always inlined.
 */
#define AIN_DEVICE __attribute__((always_inline)) __aicore__ __inline__
#endif

namespace AscendC {

/*!
 * @brief Opaque handle to a symmetric communication window.
 */
using HcommWindowHandle = __gm__ void*;

/*!
 * @brief Opaque handle to a communication team.
 */
using HcommTeamHandle = __gm__ void*;

/*!
 * @brief Placeholder tag for "no remote action" in the put primitive.
 */
typedef struct {
} AinRemoteNone;

/*!
 * @brief Placeholder tag indicating that no valid UB descriptor was supplied.
 *        Passing this type to put/get is rejected at compile time.
 */
typedef struct {
} AinDescriptorUbufNone;

/*!
 * @brief Describes the UB workspace handed to the underlying Hcomm engine.
 */
typedef struct {
    __ubuf__ uint8_t* addr; /*!< Start address of the UB buffer. */
    uint32_t bytes;         /*!< Length of the UB buffer in bytes. */
    uint32_t eventId;       /*!< Event id reserved for synchronization with the comm engine. */
} AinDescriptorUbuf;

/*!
 * @brief Commit behavior for put/get tasks.
 */
enum AinCommitFlags {
    AIN_COMMIT_IMMED = 0,         /*!< Assemble the WQE and ring the doorbell immediately. */
    AIN_COMMIT_DELAYED = (1 << 0) /*!< Assemble the WQE only; submission is deferred until a subsequent task with
                                     AIN_COMMIT_IMMED rings the doorbell. */
};

/*!
 * @brief Memory ordering for signal read/wait operations.
 * @note AIN_MEMORY_ORDER_RELAX only guarantees atomic read/write and threshold checking for the signal.
 *       It provides no memory ordering for non-signal data accesses before and after the signal operation.
 *       Use explicit barriers, pipe sync or other primitives to order ordinary data accesses.
 */
enum AinMemoryOrder { AIN_MEMORY_ORDER_RELAX };

/*!
 * @brief Signal action that atomically adds a custom value to a remote signal.
 */
typedef struct {
    HcommWindowHandle signalWindow; /*!< Target signal window. */
    size_t signalOffset;            /*!< Target signal offset. */
    uint64_t value;                 /*!< Value to add. */
} AinSignalAdd;

/*!
 * @brief Signal action that atomically increments a remote signal by 1.
 */
typedef struct {
    HcommWindowHandle signalWindow; /*!< Target signal window. */
    size_t signalOffset;            /*!< Target signal offset. */
} AinSignalInc;

/*!
 * @brief Bitmask selecting the communication engine/protocol used by Ain.
 */
enum AinCommEngineMask { AIN_MASK_DEFAULT };

} // namespace AscendC

#endif // INCLUDE_ADV_API_AIN_AIN_COMMON_H
