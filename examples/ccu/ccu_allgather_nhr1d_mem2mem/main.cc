/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <acl/acl.h>
#include <acl/acl_rt.h>
#include <hccl/hccl_comm.h>
#include <hccl/hccl_types.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "exec_op.h"

namespace CcuAgNhr1dMem2mem {

constexpr uint64_t MAX_RANKS = 16;

struct Options {
    std::vector<uint32_t> devices{0, 1};
    uint64_t count_{256};
    std::string inputDir{"./input"};
    std::string outputDir{"./output"};
};

static bool CalculateBufferSizes(uint64_t count_, uint32_t rankSize, uint64_t& inputBytes, uint64_t& outputBytes)
{
    inputBytes = 0;
    outputBytes = 0;
    if (rankSize == 0 || count_ > std::numeric_limits<uint64_t>::max() / sizeof(float)) {
        return false;
    }
    inputBytes = count_ * sizeof(float);
    if (inputBytes != 0 && rankSize > std::numeric_limits<uint64_t>::max() / inputBytes) {
        inputBytes = 0;
        return false;
    }
    outputBytes = inputBytes * rankSize;
    return outputBytes <= std::numeric_limits<size_t>::max();
}

static bool ReadFloatData(const std::string& filePath, uint64_t count_, std::vector<float>& data)
{
    if (count_ > std::numeric_limits<size_t>::max() / sizeof(float) ||
        count_ > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(float)) {
        std::cerr << "input data is too large: " << filePath << "\n";
        return false;
    }

    std::ifstream input(filePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        std::cerr << "failed to open input file: " << filePath << "\n";
        return false;
    }

    const uint64_t expectedBytes = count_ * sizeof(float);
    const std::streampos fileSize = input.tellg();
    if (fileSize < 0 || static_cast<uint64_t>(fileSize) != expectedBytes) {
        std::cerr << "input file size mismatch: " << filePath << ", expected_ " << expectedBytes << " bytes, got "
                  << fileSize << " bytes\n";
        return false;
    }

    input.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(count_));
    if (expectedBytes != 0 &&
        !input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(expectedBytes))) {
        std::cerr << "failed to read input file: " << filePath << "\n";
        return false;
    }
    return input.good() || input.eof();
}

static bool WriteFloatData(const std::string& filePath, const std::vector<float>& data)
{
    if (data.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(float)) {
        std::cerr << "output data is too large: " << filePath << "\n";
        return false;
    }

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        std::cerr << "failed to open output file: " << filePath << "\n";
        return false;
    }

    const auto bytes = static_cast<std::streamsize>(data.size() * sizeof(float));
    if (bytes != 0) {
        output.write(reinterpret_cast<const char*>(data.data()), bytes);
    }
    if (!output.good()) {
        std::cerr << "failed to write output file: " << filePath << "\n";
        return false;
    }
    return true;
}

static std::vector<uint32_t> ParseDevices(const std::string& value)
{
    std::vector<uint32_t> result;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            result.push_back(static_cast<uint32_t>(std::stoul(item)));
        }
    }
    return result;
}

static int RunRank(
    HcclRootInfo* rootInfo, uint32_t rankId, uint32_t rankSize, uint32_t device, uint64_t count_,
    const std::string& inputDir, const std::string& outputDir)
{
    int ret = 1;
    HcclComm comm = nullptr;
    aclrtStream stream = nullptr;
    void* input = nullptr;
    void* output = nullptr;

    if (aclrtSetDevice(static_cast<int32_t>(device)) != ACL_SUCCESS) {
        return 1;
    }

    if (HcclCommInitRootInfo(rankSize, rootInfo, rankId, &comm) != HCCL_SUCCESS) {
        goto cleanup;
    }

    if (aclrtCreateStream(&stream) != ACL_SUCCESS) {
        goto cleanup;
    }

    {
        uint64_t inputBytes = 0;
        uint64_t outputBytes = 0;
        if (!CalculateBufferSizes(count_, rankSize, inputBytes, outputBytes)) {
            std::cerr << "count_ too large for rankSize " << rankSize << "\n";
            goto cleanup;
        }
        const uint64_t inputAlloc = std::max<uint64_t>(inputBytes, sizeof(float));
        const uint64_t outputAlloc = std::max<uint64_t>(outputBytes, sizeof(float));
        if (aclrtMalloc(&input, inputAlloc, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
            goto cleanup;
        }
        if (aclrtMalloc(&output, outputAlloc, ACL_MEM_MALLOC_HUGE_FIRST) != ACL_SUCCESS) {
            goto cleanup;
        }

        std::vector<float> hostInput;
        const std::string inputPath = inputDir + "/input_rank_" + std::to_string(rankId) + ".bin";
        if (!ReadFloatData(inputPath, count_, hostInput)) {
            goto cleanup;
        }
        if (count_ != 0 &&
            aclrtMemcpy(input, inputBytes, hostInput.data(), inputBytes, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS) {
            goto cleanup;
        }
        if (aclrtMemset(output, outputAlloc, 0, outputAlloc) != ACL_SUCCESS) {
            goto cleanup;
        }

        if (AllGatherNhr1D(input, output, count_, rankId, rankSize, comm, stream) != HCCL_SUCCESS) {
            std::cerr << "rank " << rankId << " AllGatherNhr1D failed\n";
            goto cleanup;
        }
        std::cout << "rank " << rankId << ": launched, waiting for stream" << std::endl;
        if (aclrtSynchronizeStream(stream) != ACL_SUCCESS) {
            std::cerr << "rank " << rankId << " stream sync failed\n";
            goto cleanup;
        }
        std::cout << "rank " << rankId << ": stream completed" << std::endl;

        std::vector<float> result(count_ * rankSize);
        if (!result.empty() &&
            aclrtMemcpy(result.data(), outputBytes, output, outputBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
            goto cleanup;
        }

        constexpr uint64_t dumpCount = 16;
        std::ostringstream os;
        os << std::setprecision(9);
        os << "rank " << rankId << " input : [";
        for (uint64_t i = 0; i < count_ && i < dumpCount; ++i) {
            os << " " << hostInput[i];
        }
        if (count_ > dumpCount) {
            os << " ...(" << count_ << ")";
        }
        os << " ]\n";
        os << "rank " << rankId << " output: [";
        for (uint64_t i = 0; i < result.size() && i < dumpCount; ++i) {
            os << " " << result[i];
        }
        if (result.size() > dumpCount) {
            os << " ...(" << result.size() << ")";
        }
        os << " ]";
        std::cout << os.str() << std::endl;

        const std::string outputPath = outputDir + "/output_rank_" + std::to_string(rankId) + ".bin";
        if (!WriteFloatData(outputPath, result)) {
            goto cleanup;
        }
        std::cout << "rank " << rankId << ": output saved to " << outputPath << "\n";
        ret = 0;
    }

cleanup:
    if (output) {
        (void)aclrtFree(output);
    }
    if (input) {
        (void)aclrtFree(input);
    }
    if (stream) {
        (void)aclrtDestroyStream(stream);
    }
    if (comm) {
        (void)HcclCommDestroy(comm);
    }
    (void)aclrtResetDevice(static_cast<int32_t>(device));
    return ret;
}

static Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--devices" && i + 1 < argc) {
            options.devices = ParseDevices(argv[++i]);
        } else if (arg == "--count" && i + 1 < argc) {
            options.count_ = std::stoull(argv[++i]);
        } else if (arg == "--input-dir" && i + 1 < argc) {
            options.inputDir = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.outputDir = argv[++i];
        } else {
            throw std::invalid_argument("usage: ccu_allgather_nhr1d_mem2mem [--devices 0,1] "
                                        "[--count 256] [--input-dir ./input] "
                                        "[--output-dir ./output]");
        }
    }
    return options;
}

static int run(int argc, char** argv)
{
    Options options;
    try {
        options = ParseOptions(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }
    if (options.devices.empty() || options.devices.size() > MAX_RANKS) {
        std::cerr << "rank count must be in [1, " << MAX_RANKS << "]\n";
        return 1;
    }
    const std::set<uint32_t> uniqueDevices(options.devices.begin(), options.devices.end());
    if (uniqueDevices.size() != options.devices.size()) {
        std::cerr << "device list contains duplicate device IDs\n";
        return 1;
    }

    if (aclInit(nullptr) != ACL_SUCCESS) {
        return 1;
    }
    if (aclrtSetDevice(static_cast<int32_t>(options.devices.front())) != ACL_SUCCESS) {
        (void)aclFinalize();
        return 1;
    }

    void* rootStorage = nullptr;
    if (aclrtMallocHost(&rootStorage, sizeof(HcclRootInfo)) != ACL_SUCCESS) {
        (void)aclFinalize();
        return 1;
    }
    auto* rootInfo = static_cast<HcclRootInfo*>(rootStorage);
    if (HcclGetRootInfo(rootInfo) != HCCL_SUCCESS) {
        (void)aclrtFreeHost(rootStorage);
        (void)aclFinalize();
        return 1;
    }

    const uint32_t rankSize = static_cast<uint32_t>(options.devices.size());
    std::vector<std::thread> workers;
    std::vector<int> status(rankSize, 1);
    for (uint32_t rank = 0; rank < rankSize; ++rank) {
        workers.emplace_back([&, rank]() {
            status[rank] = RunRank(
                rootInfo, rank, rankSize, options.devices[rank], options.count_, options.inputDir, options.outputDir);
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    (void)aclrtFreeHost(rootStorage);
    (void)aclFinalize();
    return std::any_of(status.begin(), status.end(), [](int value) { return value != 0; }) ? 1 : 0;
}

} // namespace CcuAgNhr1dMem2mem

int main(int argc, char** argv) { return CcuAgNhr1dMem2mem::run(argc, argv); }
