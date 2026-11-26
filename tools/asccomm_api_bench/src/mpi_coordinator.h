/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_API_BENCH_MPI_COORDINATOR_H
#define ASCCOMM_API_BENCH_MPI_COORDINATOR_H

#include <mpi.h>
#include "session.h"

namespace asccomm_api_bench {

// Borrows an initialized MPI communicator. Does not initialize MPI, create a
// new job, or finalize MPI. Transport errors throw; the owner must abort.
class MpiCoordinator final : public Coordinator {
public:
    explicit MpiCoordinator(MPI_Comm comm);
    uint32_t Rank() const override { return rank_; }
    uint32_t Size() const override { return size_; }
    bool AllTrue(bool local_value) override;
    std::string Broadcast(const std::string& root_value) override;

private:
    MPI_Comm comm_;
    uint32_t rank_;
    uint32_t size_;
};

} // namespace asccomm_api_bench
#endif
