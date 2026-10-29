/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_runtime_backend.h"

#if defined(CCU_TRANSLATOR_HAS_HCOMM_BACKEND)
#include "ccu_runtime_hcomm_backend.h"
#endif

namespace ccu::runtime {
namespace {

#if !defined(CCU_TRANSLATOR_HAS_HCOMM_BACKEND)
class UnavailableBackend final : public Backend {
public:
    ccu_status_t contextCreate(void*, void**, uint32_t*, uint32_t*) noexcept override
    {
        return CCU_STATUS_RUNTIME_ERROR;
    }
    ccu_status_t contextDestroy(void*) noexcept override { return CCU_STATUS_RUNTIME_ERROR; }
    ccu_status_t externalEventVa(void*, uint32_t, void**, uint64_t*) noexcept override
    {
        return CCU_STATUS_RUNTIME_ERROR;
    }
    ccu_status_t externalXnVa(void*, uint32_t, void**, uint64_t*) noexcept override { return CCU_STATUS_RUNTIME_ERROR; }
    ccu_status_t launch(
        void*, const ccu_kernel_descriptor_t*, const ccu_rank_entry_t*, void*, HcomCcuSchd, const ccu_task_arg_t*,
        uint32_t, const BackendExternalArg*, uint32_t, void**) noexcept override
    {
        return CCU_STATUS_RUNTIME_ERROR;
    }
    BackendSyncResult synchronize(void*, uint64_t, uint64_t) noexcept override
    {
        return {CCU_STATUS_RUNTIME_ERROR, CCU_ABORT_NONE, true};
    }
    ccu_status_t launchDestroy(void*) noexcept override { return CCU_STATUS_RUNTIME_ERROR; }
};
#endif

Backend* testingBackend = nullptr;

} // namespace

Backend& systemBackend() noexcept
{
#if defined(CCU_TRANSLATOR_HAS_HCOMM_BACKEND)
    return testingBackend ? *testingBackend : hcommBackend();
#else
    static UnavailableBackend unavailableBackend;
    return testingBackend ? *testingBackend : unavailableBackend;
#endif
}

void setBackendForTesting(Backend* backend) noexcept { testingBackend = backend; }

} // namespace ccu::runtime
