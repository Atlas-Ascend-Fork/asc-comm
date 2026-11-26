/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "mpi_context.h"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "mpi_coordinator.h"
#include "options.h"

namespace asccomm_api_bench {
namespace {
void CheckMpi(int ret, const char* operation)
{
    if (ret != MPI_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
}
} // namespace

RankContext DiscoverMpiContext(MPI_Comm comm, const Options& options)
{
    MpiCoordinator coordinator(comm);
    std::string local_error;
    try {
        ValidateOptions(options);
        if (options.ranks != coordinator.Size() || options.ranks % options.devices_per_node != 0)
            throw std::invalid_argument("MPI world must match -n and be divisible by -p");
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    if (!coordinator.AllTrue(local_error.empty()))
        throw std::invalid_argument(
            local_error.empty() ? "another rank rejected MPI topology parameters" : local_error);

    MPI_Comm local_comm = MPI_COMM_NULL;
    CheckMpi(
        MPI_Comm_split_type(
            comm, MPI_COMM_TYPE_SHARED, static_cast<int>(coordinator.Rank()), MPI_INFO_NULL, &local_comm),
        "MPI_Comm_split_type(shared)");
    int local_rank = 0;
    int local_size = 0;
    CheckMpi(MPI_Comm_rank(local_comm, &local_rank), "MPI_Comm_rank(local)");
    CheckMpi(MPI_Comm_size(local_comm, &local_size), "MPI_Comm_size(local)");
    int node_leader = static_cast<int>(coordinator.Rank());
    CheckMpi(MPI_Bcast(&node_leader, 1, MPI_INT, 0, local_comm), "MPI_Bcast(node leader)");

    char processor[MPI_MAX_PROCESSOR_NAME] = {};
    int processor_length = 0;
    CheckMpi(MPI_Get_processor_name(processor, &processor_length), "MPI_Get_processor_name");
    std::vector<int> leaders;
    std::vector<char> processors;
    try {
        if (processor_length <= 0 || processor_length > MPI_MAX_PROCESSOR_NAME)
            throw std::invalid_argument("MPI returned an invalid processor-name length");
        std::fill(processor + processor_length, processor + MPI_MAX_PROCESSOR_NAME, '\0');
        leaders.resize(coordinator.Size());
        if (coordinator.Size() > processors.max_size() / MPI_MAX_PROCESSOR_NAME)
            throw std::length_error("MPI processor-name storage exceeds the allocation limit");
        processors.resize(static_cast<size_t>(coordinator.Size()) * MPI_MAX_PROCESSOR_NAME);
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    if (!coordinator.AllTrue(local_error.empty())) {
        CheckMpi(MPI_Comm_free(&local_comm), "MPI_Comm_free(local)");
        throw std::runtime_error(
            local_error.empty() ? "another rank could not prepare MPI topology storage" : local_error);
    }
    CheckMpi(MPI_Allgather(&node_leader, 1, MPI_INT, leaders.data(), 1, MPI_INT, comm), "MPI_Allgather(node leaders)");
    CheckMpi(
        MPI_Allgather(
            processor, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, processors.data(), MPI_MAX_PROCESSOR_NAME, MPI_CHAR, comm),
        "MPI_Allgather(processor names)");
    RankContext context;
    try {
        // A shared-memory communicator need not contain every rank on a host.
        // For example, MPICH ch3:sock returns MPI_COMM_SELF for every rank.
        // Reject such partitions instead of treating one host as several nodes
        // and assigning the same local device to different ranks.
        std::map<std::string, int> processor_leaders;
        for (size_t rank = 0; rank < leaders.size(); ++rank) {
            const char* first = processors.data() + rank * MPI_MAX_PROCESSOR_NAME;
            const char* last = std::find(first, first + MPI_MAX_PROCESSOR_NAME, '\0');
            const std::string name(first, last);
            const auto inserted = processor_leaders.emplace(name, leaders[rank]);
            if (!inserted.second && inserted.first->second != leaders[rank])
                throw std::invalid_argument(
                    "MPI_COMM_TYPE_SHARED splits processor '" + name +
                    "' into multiple groups; use an MPI build with working per-node shared-memory support");
        }
        std::vector<int> unique_leaders = leaders;
        std::sort(unique_leaders.begin(), unique_leaders.end());
        unique_leaders.erase(std::unique(unique_leaders.begin(), unique_leaders.end()), unique_leaders.end());
        context.global_rank = coordinator.Rank();
        context.world_size = coordinator.Size();
        context.local_rank = static_cast<uint32_t>(local_rank);
        context.local_size = static_cast<uint32_t>(local_size);
        context.node_index = static_cast<uint32_t>(
            std::lower_bound(unique_leaders.begin(), unique_leaders.end(), node_leader) - unique_leaders.begin());
        context.node_count = static_cast<uint32_t>(unique_leaders.size());
        context.device_id = context.local_rank;
        context.hostname.assign(processor, static_cast<size_t>(processor_length));
        ValidateRankContext(options, context);
    } catch (const std::exception& error) {
        local_error = error.what();
    }
    const bool valid = coordinator.AllTrue(local_error.empty());
    // Free only on the normal, coordinated path. A transport exception above
    // must reach the owner's MPI_Abort without entering another collective.
    CheckMpi(MPI_Comm_free(&local_comm), "MPI_Comm_free(local)");
    if (!valid)
        throw std::invalid_argument(local_error.empty() ? "another rank has incompatible MPI placement" : local_error);
    return context;
}

} // namespace asccomm_api_bench
