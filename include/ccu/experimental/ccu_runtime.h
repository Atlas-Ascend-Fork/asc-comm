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

#define CCU_RUNTIME_API_VERSION 1
#define CCU_OBJECT_ABI_VERSION 1
#define CCU_STUB_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ccu_context ccu_context_t;

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
 * The generated host stub also copies these into the kernel descriptor's
 * schd_channel_size / schd_channel_list fields so the provider can consume
 * them at launch time. */
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

/* Opaque instance handle.  The value may be supplied by HComm and is bound to
 * a ccu_context_t via ccu_context_bind_instance; it is passed unchanged as
 * the second chevron slot. */
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

typedef enum ccu_abort_action_t {
    CCU_ABORT_NONE = 0,
    CCU_ABORT_STREAM_ONLY = 1,
    CCU_ABORT_DEVICE = 2
} ccu_abort_action_t;

typedef struct ccu_context_options_t {
    uint32_t struct_size;
    uint32_t api_version;
    void* communicator;
    uint64_t default_sync_timeout_ms;
    uint64_t abort_timeout_ms;
    uint64_t reserved[4];
} ccu_context_options_t;

typedef struct ccu_buffer_view_t {
    uint64_t address;
    uint64_t size;
    uint64_t token;
} ccu_buffer_view_t;

typedef enum ccu_task_arg_kind_t { CCU_TASK_ARG_U64 = 1, CCU_TASK_ARG_I64 = 2 } ccu_task_arg_kind_t;

typedef struct ccu_task_arg_schema_t {
    uint32_t kind;
    uint32_t flags;
    const char* name;
} ccu_task_arg_schema_t;

typedef struct ccu_task_arg_t {
    uint64_t bits;
} ccu_task_arg_t;

typedef enum ccu_specialization_kind_t {
    CCU_SPECIALIZATION_UNSIGNED = 1,
    CCU_SPECIALIZATION_SIGNED = 2,
    CCU_SPECIALIZATION_ENUM = 3
} ccu_specialization_kind_t;

typedef struct ccu_specialization_value_t {
    uint32_t kind;
    uint32_t bit_width;
    uint64_t bits;
    const char* type_identity;
} ccu_specialization_value_t;

typedef struct ccu_image_descriptor_t {
    uint32_t struct_size;
    uint32_t object_abi_version;
    uint32_t ccuobj_abi_revision;
    uint32_t flags;
    const char* target;
    const uint8_t* ccuobj;
    uint64_t ccuobj_size;
    const uint8_t* sha256;
} ccu_image_descriptor_t;

typedef struct ccu_rank_entry_t {
    uint32_t communicator_rank;
    uint32_t kernel_index;
    uint64_t payload_identity;
} ccu_rank_entry_t;

typedef struct ccu_kernel_descriptor_t {
    uint32_t struct_size;
    uint32_t stub_abi_version;
    const char* family_identity;
    const char* specialization_identity;
    const ccu_image_descriptor_t* image;
    const ccu_specialization_value_t* specialization_values;
    uint32_t specialization_value_count;
    uint32_t communicator_size;
    const ccu_rank_entry_t* rank_entries;
    uint32_t rank_entry_count;
    const ccu_task_arg_schema_t* task_args;
    uint32_t task_arg_count;
    uint32_t implicit_peer_count;
    /* schd channel passthrough.  The generated host stub copies
     * HcomCcuSchd.channelSize / channelList into these fields so the runtime
     * can consume them without a separate API call.  When schd_channel_size
     * is 0 (or schd_channel_list is NULL) the runtime falls back to its
     * internal HcclChannelAcquire path.  Occupies former reserved[0..1]. */
    uint64_t schd_channel_size;
    const void* schd_channel_list;
    uint64_t reserved[2];
} ccu_kernel_descriptor_t;

/* ABI of every generated __ccu_stub_<specialization-sha256> symbol. */
typedef const ccu_kernel_descriptor_t* (*ccu_stub_fn_t)(void);

ccu_status_t ccu_context_create(const ccu_context_options_t* options, ccu_context_t** context);
ccu_status_t ccu_context_destroy(ccu_context_t** context);

/* Synchronize and release the current CCU mission submitted on stream. */
ccu_status_t ccu_synchronize(ccu_context_t* context, void* stream, uint64_t timeout_ms);

/* Bind an instance handle to a context for 3-slot launch
 * (<<<schd, instance, stream>>>).  A non-zero HComm-owned handle is preserved;
 * zero requests a facade-local handle for compatibility with existing code.
 * The generated dispatcher uses the value to look up the context at submit
 * time. */
ccu_status_t ccu_context_bind_instance(ccu_context_t* context, ccu_instance_t* instance);

const char* ccu_status_string(ccu_status_t status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CCU_RUNTIME_H */
