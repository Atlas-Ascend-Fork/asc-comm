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

// Deprecation attribute for the legacy PascalCase ain interfaces. Definition mirrors the devkit
// c_api/defs/macro.h so both headers can coexist in one translation unit.
#ifndef ASC_DEPRECATED
#define ASC_DEPRECATED(version, removal_date, replacement)                                                  \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif
#ifndef AIN_DEVICE
/*!
 * @brief Force-inline qualifier for ain device-side member functions.
 *        All ain methods run on AICORE and are always inlined.
 */
#define AIN_DEVICE __attribute__((always_inline)) __aicore__ __inline__
#endif

namespace asc {

/*!
 * @brief Opaque handle to a symmetric communication window.
 */
using HcclCommSymWindow = __gm__ void*;

/*!
 * @brief Opaque handle to a symmetric communication virtual memory.
 */
using HcommMemHandle = __gm__ void*;

/*!
 * @brief Opaque handle to a communication team.
 */
using HcommTeamHandle = __gm__ void*;

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
 * @brief Describes the UB workspace handed to the underlying Hcomm engine.
 */
typedef struct {
    __ubuf__ uint8_t* addr; /*!< Start address of the UB buffer. */
    uint32_t bytes;         /*!< Length of the UB buffer in bytes. */
    uint32_t event_id;      /*!< Event id reserved for synchronization with the comm engine. */
} ain_descriptor_ubuf;

/*!
 * @brief Commit behavior for put/get tasks.
 */
enum ain_commit_flags {
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
enum ain_memory_order { AIN_MEMORY_ORDER_RELAX };

/*!
 * @brief Signal action that atomically adds a custom value to a remote signal.
 */
typedef struct {
    HcclCommSymWindow signal_window; /*!< Target signal window. */
    size_t signal_offset;            /*!< Target signal offset. */
    uint64_t value;                  /*!< Value to add. */
} ain_signal_add;

/*!
 * @brief Signal action that atomically increments a remote signal by 1.
 */
typedef struct {
    HcclCommSymWindow signal_window; /*!< Target signal window. */
    size_t signal_offset;            /*!< Target signal offset. */
} ain_signal_inc;

/*!
 * @brief Bitmask selecting the communication engine/protocol used by ain.
 */
enum ain_comm_engine_mask { AIN_MASK_DEFAULT };

} // namespace asc

// Compatibility bridge for pre-rename AscendC-namespace spellings.
namespace AscendC {
using asc::ain_comm_engine_mask;
using asc::AIN_COMMIT_DELAYED;
using asc::ain_commit_flags;
using asc::AIN_COMMIT_IMMED;
using asc::ain_descriptor_ubuf;
using asc::ain_descriptor_ubuf_none;
using asc::AIN_MASK_DEFAULT;
using asc::ain_memory_order;
using asc::AIN_MEMORY_ORDER_RELAX;
using asc::ain_remote_none;
using asc::ain_signal_add;
using asc::ain_signal_inc;
using asc::HcclCommSymWindow;
using asc::HcommMemHandle;
using asc::HcommTeamHandle;
using AinRemoteNone = asc::ain_remote_none;
using AinDescriptorUbufNone = asc::ain_descriptor_ubuf_none;
using AinDescriptorUbuf = asc::ain_descriptor_ubuf;
using AinCommitFlags = asc::ain_commit_flags;
using AinMemoryOrder = asc::ain_memory_order;
using AinSignalAdd = asc::ain_signal_add;
using AinSignalInc = asc::ain_signal_inc;
using AinCommEngineMask = asc::ain_comm_engine_mask;
} // namespace AscendC

#endif // INCLUDE_COMM_API_AICORE_AIN_AIN_COMMON_H
