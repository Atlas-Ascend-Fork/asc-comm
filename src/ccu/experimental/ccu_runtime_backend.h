/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RUNTIME_BACKEND_H
#define CCU_RUNTIME_BACKEND_H

#include "ccu/experimental/ccu_runtime.h"

#include <cstdint>

namespace ccu::runtime {

struct BackendSyncResult {
    ccu_status_t status = CCU_STATUS_RUNTIME_ERROR;
    ccu_abort_action_t abortAction = CCU_ABORT_NONE;
    bool terminal = false;
};

struct BackendExternalArg {
    uint32_t kind = 0;
    uint32_t count = 0;
    void* set = nullptr;
};

class Backend {
public:
    virtual ~Backend() = default;

    // Resource-producing calls are transactional: on failure, output handles
    // remain null and the backend has fully rolled back partial work.
    virtual ccu_status_t contextCreate(
        void* communicator, void** context, uint32_t* rank, uint32_t* rankSize) noexcept = 0;
    virtual ccu_status_t contextDestroy(void* context) noexcept = 0;
    virtual ccu_status_t bindInstance(void* context, ccu_instance_t instance) noexcept
    {
        (void)context;
        (void)instance;
        return CCU_STATUS_SUCCESS;
    }
    virtual ccu_status_t externalEventVa(void* context, uint32_t count, void** eventSet, uint64_t* vas) noexcept = 0;
    virtual ccu_status_t externalXnVa(void* context, uint32_t count, void** xnSet, uint64_t* vas) noexcept = 0;
    virtual ccu_status_t launch(
        void* context, const ccu_kernel_descriptor_t* descriptor, const ccu_rank_entry_t* rankEntry, void* stream,
        HcomCcuSchd schd, const ccu_task_arg_t* arguments, uint32_t argumentCount,
        const BackendExternalArg* externalArguments, uint32_t externalArgumentCount, void** launch) noexcept = 0;
    virtual BackendSyncResult synchronize(void* launch, uint64_t syncTimeoutMs, uint64_t abortTimeoutMs) noexcept = 0;
    virtual ccu_status_t launchDestroy(void* launch) noexcept = 0;
};

Backend& systemBackend() noexcept;

// Private test seam. It is not installed and is never part of the public C ABI.
void setBackendForTesting(Backend* backend) noexcept;

} // namespace ccu::runtime

#endif // CCU_RUNTIME_BACKEND_H
