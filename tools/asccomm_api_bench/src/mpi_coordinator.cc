/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "mpi_coordinator.h"

#include <stdexcept>

namespace asccomm_api_bench {
namespace {
void CheckMpi(int ret, const char* operation)
{
    if (ret != MPI_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
    }
}
} // namespace

MpiCoordinator::MpiCoordinator(MPI_Comm comm) : comm_(comm), rank_(0), size_(0)
{
    int rank = 0;
    int size = 0;
    CheckMpi(MPI_Comm_rank(comm_, &rank), "MPI_Comm_rank");
    CheckMpi(MPI_Comm_size(comm_, &size), "MPI_Comm_size");
    if (rank < 0 || size < 1)
        throw std::runtime_error("invalid MPI communicator");
    rank_ = static_cast<uint32_t>(rank);
    size_ = static_cast<uint32_t>(size);
}

bool MpiCoordinator::AllTrue(bool local_value)
{
    int local = local_value ? 1 : 0;
    int global = 0;
    CheckMpi(MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, comm_), "MPI_Allreduce");
    return global != 0;
}

std::string MpiCoordinator::Broadcast(const std::string& root_value)
{
    // All ranks check the bound after receiving size, before allocating or
    // entering a different collective. Keep the count within MPI int range.
    unsigned long long bytes = rank_ == 0 ? root_value.size() : 0;
    CheckMpi(MPI_Bcast(&bytes, 1, MPI_UNSIGNED_LONG_LONG, 0, comm_), "MPI_Bcast(size)");
    if (bytes > 64ULL * 1024 * 1024)
        throw std::runtime_error("MPI plan record exceeds 64MiB");
    std::string value(static_cast<size_t>(bytes), '\0');
    if (rank_ == 0)
        value = root_value;
    if (bytes != 0) {
        CheckMpi(MPI_Bcast(&value[0], static_cast<int>(bytes), MPI_CHAR, 0, comm_), "MPI_Bcast(data)");
    }
    return value;
}

} // namespace asccomm_api_bench
