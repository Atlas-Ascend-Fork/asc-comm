/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RUNTIME_H
#define CCU_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Schedule descriptor for CCU kernel launch (<<<schd, instance, stream>>>).
 * Carries runtime control-plane values.  The struct follows the expert-
 * reviewed 7-field layout: missionNum, missionStart, rankId, rankSize,
 * cacheTag, channelSize, channelList.  The phyDieMask field from the
 * original ccu 1.0 layout has been removed.
 *
 * When a kernel is declared with __ccu_rank_sizes__(2,4,8), the compiler
 * instantiates all candidates and the runtime dispatcher selects the matching
 * specialization via schd.rankSize.  rankId (3rd field, 0 for legacy code)
 * is used by the runtime to override the per-rank kernel image selection.
 * channelList / channelSize optionally carry explicit peer channel handles;
 * when channelList is NULL the runtime falls back to its internal channel
 * acquisition (HcclChannelAcquire).  When non-NULL, the runtime fills the
 * internal peerChannels table directly from channelList (an array of
 * channelSize ChannelHandle values), skipping HcclChannelAcquire entirely.
 */
typedef struct HcomCcuSchd {
    uint32_t missionNum;
    uint32_t missionStart;
    uint32_t rankId;
    uint32_t rankSize;
    uint64_t cacheTag;
    uint64_t channelSize;
    void* channelList;
} HcomCcuSchd;

/* Compiler-recognized attribute: declares the candidate rank sizes a
 * __global__ __ccu__ kernel supports.  Expands to nothing for the host
 * compiler; ccuc scans it lexically to drive per-rankSize instantiation
 * and runtime dispatch. */
#define __ccu_rank_sizes__(...)

/* Opaque HComm instance handle, passed unchanged as the second chevron slot. */
typedef uint64_t ccu_instance_t;

typedef enum ccu_status_t {
    CCU_STATUS_SUCCESS = 0,
    CCU_STATUS_INVALID_ARGUMENT = 1,
    CCU_STATUS_VERSION_MISMATCH = 2,
    CCU_STATUS_INVALID_OBJECT = 3,
    CCU_STATUS_KERNEL_NOT_FOUND = 4,
    CCU_STATUS_SCHEMA_MISMATCH = 5,
    CCU_STATUS_RESOURCE_EXHAUSTED = 6,
    CCU_STATUS_RUNTIME_ERROR = 7,
    CCU_STATUS_TIMEOUT_RECOVERED = 8,
    CCU_STATUS_RECOVERY_FAILED = 9,
    CCU_STATUS_BUSY = 10,
    CCU_STATUS_CONTEXT_POISONED = 11
} ccu_status_t;

typedef struct ccu_buffer_view_t {
    uint64_t address;
    uint64_t size;
    uint64_t token;
} ccu_buffer_view_t;

/* Synchronize the mission submitted for this HComm instance on stream. */
ccu_status_t ccu_synchronize(ccu_instance_t instance, void* stream, uint64_t timeout_ms);

const char* ccu_status_string(ccu_status_t status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CCU_RUNTIME_H */
