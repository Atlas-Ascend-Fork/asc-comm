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
#include "mpi_coordinator.h"
#include "options.h"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS)
        return 1;
    if (MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN) != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    try {
        // An operator is needed by the shared option validator, but this
        // diagnostic never initializes ACL/HCCL or executes an operator.
        std::vector<std::string> args = {"--op", "allgather"};
        for (int index = 1; index < argc; ++index) {
            const std::string argument(argv[index]);
            if (argument == "--help") {
                int rank = 0;
                if (MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS)
                    throw std::runtime_error("MPI_Comm_rank failed");
                if (rank == 0)
                    std::cout << "asccomm_api_bench_mpi_check -n total_ranks -p ranks_per_node\n"
                                 "Checks actual MPI node membership; does not test NPU communication.\n";
                return MPI_Finalize() == MPI_SUCCESS ? 0 : 1;
            }
            args.push_back(argument);
        }
        const auto options = asccomm_api_bench::ParseOptions(args);
        const auto context = asccomm_api_bench::DiscoverMpiContext(MPI_COMM_WORLD, options);
        asccomm_api_bench::MpiCoordinator coordinator(MPI_COMM_WORLD);
        const std::string expected("MC2\0MPI", 7);
        const std::string received = coordinator.Broadcast(context.global_rank == 0 ? expected : std::string());
        if (!coordinator.AllTrue(received == expected))
            throw std::runtime_error("MPI binary bootstrap transport check failed");
        std::cout << "MPI topology OK global_rank=" << context.global_rank << " world_size=" << context.world_size
                  << " local_rank=" << context.local_rank << " local_size=" << context.local_size
                  << " node_index=" << context.node_index << " node_count=" << context.node_count
                  << " hostname=" << context.hostname << " device_id=" << context.device_id << '\n';
    } catch (const std::exception& error) {
        std::cerr << "MPI topology check failed: " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    return MPI_Finalize() == MPI_SUCCESS ? 0 : 1;
}
