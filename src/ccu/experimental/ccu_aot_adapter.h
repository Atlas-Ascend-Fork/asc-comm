/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file ccu_aot_adapter.h
 * @brief CcuAotKernel AOT adapter — 让 AOT .ccuobj 走 HComm 正式路径
 *
 * 用法：
 *   #include "ccu_aot_adapter.h"
 *
 *   CcuAotContext *ctx = nullptr;
 *   ccu_aot_attach_current(&ctx, comm, &rank, &worldSize);
 *   ccu_aot_register_kernel_with_external(...); // Register + relocation
 *   ccu_aot_launch_registered_on_stream(...);   // Launch
 *   ccu_aot_finalize(ctx);                      // Destroy
 *
 * 编译：链接 libhcomm.so + libascendcl.so + libruntime.so
 */

#ifndef CCU_AOT_ADAPTER_H
#define CCU_AOT_ADAPTER_H

#include <stdint.h>
#include <stddef.h>

#include "ccu/experimental/ccu_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCU_AOT_SUCCESS = 0,
    CCU_AOT_E_INIT = 1,
    CCU_AOT_E_REGISTER = 2,
    CCU_AOT_E_LAUNCH = 3,
    CCU_AOT_E_PARAM = 4,
    // The adapter proved that no device task was submitted.
    CCU_AOT_E_NOT_SUBMITTED = 5,
} CcuAotError;

typedef enum {
    CCU_AOT_ABORT_NONE = 0,
    CCU_AOT_ABORT_STREAM_ONLY = 1,
    CCU_AOT_ABORT_DEVICE = 2,
} CcuAotAbortAction;

typedef struct CcuAotSyncResult {
    CcuAotError status;
    CcuAotAbortAction abortAction;
    uint32_t runtimeCode;
    uint32_t terminal;
} CcuAotSyncResult;

typedef struct CcuAotContext CcuAotContext;

typedef struct CcuAotExternalArg {
    uint32_t kind;
    uint32_t count;
    uint64_t acquire_handle;
} CcuAotExternalArg;

/** Attach a borrowed communicator using its queried rank/size and current ACL device. */
CcuAotError ccu_aot_attach_current(CcuAotContext** ctx, void* comm, uint32_t* rank, uint32_t* worldSize);

/* Borrow channels created by the caller's HComm control-plane flow. The
 * caller retains ownership; finalize must not destroy these handles. */
CcuAotError ccu_aot_bind_channels(CcuAotContext* ctx, const uint64_t* channels, uint32_t channelCount);

/* Bind the caller-owned HComm instance selected by the public control plane.
 * This is an internal backend handoff; it does not create or destroy the
 * HComm instance and is not a user-facing HComm control-plane API. */
CcuAotError ccu_aot_bind_instance(CcuAotContext* ctx, uint32_t rank, uint64_t instanceHandle);

/**
 * Append a kernel from a .ccuobj to the context and return a context-local
 * kernel id. This entry performs registration, relocation, and instruction
 * loading without launching; finalize unregisters all registered kernels.
 *
 * 注册后的第一个 kernel 会成为 context 的默认目标。
 */
CcuAotError ccu_aot_register_kernel_with_external(
    CcuAotContext* ctx, const void* ccuobj, size_t size, uint32_t kernelIndex, const CcuAotExternalArg* externalArgs,
    uint32_t externalArgCount, uint32_t* kernelId);

/** Unregister one terminal kernel and release its instruction/resource lease. */
CcuAotError ccu_aot_unregister_kernel(CcuAotContext* ctx, uint32_t kernelId);

/**
 * Launch directly on a borrowed execution stream. Unlike the fused/AICore
 * path, this does not create an internal-CCU-stream tail dependency.
 */
CcuAotError ccu_aot_launch_registered_on_stream(
    CcuAotContext* ctx, uint32_t kernelId, uint32_t rank, const uint64_t* taskArgs, uint32_t argCount,
    const CcuAotExternalArg* externalArgs, uint32_t externalArgCount, void* executionStream);

CcuAotError ccu_aot_external_event_alloc_va(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas);
CcuAotError ccu_aot_external_xn_alloc_va(
    CcuAotContext* ctx, uint32_t rank, uint32_t count, uint64_t* handle, uint64_t* vas);

/**
 * Bounded synchronization of a borrowed direct execution stream.
 * requireDeviceRecovery continues an in-progress device-wide recovery
 * transaction; it may not be downgraded to stream-only recovery.
 */
CcuAotError ccu_aot_sync_ex_on_stream(
    CcuAotContext* ctx, uint32_t rank, void* executionStream, uint64_t syncTimeoutMs, uint64_t abortTimeoutMs,
    uint32_t requireDeviceRecovery, CcuAotSyncResult* result);

/**
 * 清理
 */
CcuAotError ccu_aot_finalize(CcuAotContext* ctx);

const char* ccu_aot_strerror(CcuAotError err);

#ifdef __cplusplus
}
#endif

#endif /* CCU_AOT_ADAPTER_H */
