/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_COMM_BOOTSTRAP_H
#define ASCCOMM_API_BENCH_COMM_BOOTSTRAP_H

#include "session.h"

namespace asccomm_api_bench {

// Callback return values are preserved. All callbacks must use the same formal
// runtime on each rank. The owner initializes ACL and binds its device first.
struct CommBootstrapApi {
    size_t root_info_bytes = 0;
    std::function<int64_t(std::string&)> get_root_info;
    // The fourth argument is HcclCommConfig::hcclOpExpansionMode, not an MC2
    // commEngine or the HcclOpExpansionMode enum returned by config queries.
    std::function<int64_t(uint32_t, const std::string&, uint32_t, uint8_t, void**)> create;
    std::function<int64_t(void*, std::string&)> get_name;
    std::function<int64_t(void*)> destroy;
};

class CommHandle {
public:
    CommHandle() = default;
    CommHandle(void* handle, std::function<int64_t(void*)> destroy);
    ~CommHandle();
    CommHandle(const CommHandle&) = delete;
    CommHandle& operator=(const CommHandle&) = delete;
    CommHandle(CommHandle&& other) noexcept;
    CommHandle& operator=(CommHandle&& other) noexcept;
    void* Get() const { return handle_; }
    const std::string& Name() const { return name_; }
    void SetName(std::string name);
    // Explicit close reports cleanup errors. Destruction is best-effort and
    // never throws; close before finalizing the device runtime.
    int64_t Close() noexcept;

private:
    void* handle_ = nullptr;
    std::function<int64_t(void*)> destroy_;
    std::string name_;
};

struct CommBootstrapResult {
    CommHandle communicator;
    std::string failure_stage;
    std::string error;
    int64_t ret = 0;
    int64_t cleanup_ret = 0;
    bool Ok() const { return failure_stage.empty(); }
};

// All ranks participate in the same sequence of collectives. Root-info bytes
// are broadcast as binary data, including NULs. A local API failure prevents
// every rank from proceeding, and successfully created handles are cleaned up.
// Coordinator transport exceptions propagate; callers must abort the MPI job.
CommBootstrapResult BootstrapCommunicator(
    const Options& options, Coordinator& coordinator, const CommBootstrapApi& api);

} // namespace asccomm_api_bench
#endif
