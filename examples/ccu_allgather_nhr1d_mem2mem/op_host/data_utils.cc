/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "data_utils.h"

#include <fstream>
#include <iostream>
#include <limits>

namespace CcuAgNhr1dMem2mem {

bool CalculateBufferSizes(
    uint64_t count, uint32_t rankSize, uint64_t& inputBytes,
    uint64_t& outputBytes)
{
    inputBytes = 0;
    outputBytes = 0;
    if (rankSize == 0 ||
        count > std::numeric_limits<uint64_t>::max() / sizeof(float)) {
        return false;
    }
    inputBytes = count * sizeof(float);
    if (inputBytes != 0 &&
        rankSize > std::numeric_limits<uint64_t>::max() / inputBytes) {
        inputBytes = 0;
        return false;
    }
    outputBytes = inputBytes * rankSize;
    return outputBytes <= std::numeric_limits<size_t>::max();
}

bool ReadFloatData(
    const std::string& filePath, uint64_t count, std::vector<float>& data)
{
    if (count > std::numeric_limits<size_t>::max() / sizeof(float) ||
        count > static_cast<uint64_t>(
            std::numeric_limits<std::streamsize>::max()) / sizeof(float)) {
        std::cerr << "input data is too large: " << filePath << "\n";
        return false;
    }

    std::ifstream input(filePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        std::cerr << "failed to open input file: " << filePath << "\n";
        return false;
    }

    const uint64_t expectedBytes = count * sizeof(float);
    const std::streampos fileSize = input.tellg();
    if (fileSize < 0 || static_cast<uint64_t>(fileSize) != expectedBytes) {
        std::cerr << "input file size mismatch: " << filePath
                  << ", expected " << expectedBytes << " bytes, got "
                  << fileSize << " bytes\n";
        return false;
    }

    input.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(count));
    if (expectedBytes != 0 &&
        !input.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(expectedBytes))) {
        std::cerr << "failed to read input file: " << filePath << "\n";
        return false;
    }
    return input.good() || input.eof();
}

bool WriteFloatData(
    const std::string& filePath, const std::vector<float>& data)
{
    if (data.size() >
        static_cast<size_t>(std::numeric_limits<std::streamsize>::max()) /
            sizeof(float)) {
        std::cerr << "output data is too large: " << filePath << "\n";
        return false;
    }

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        std::cerr << "failed to open output file: " << filePath << "\n";
        return false;
    }

    const auto bytes =
        static_cast<std::streamsize>(data.size() * sizeof(float));
    if (bytes != 0) {
        output.write(reinterpret_cast<const char*>(data.data()), bytes);
    }
    if (!output.good()) {
        std::cerr << "failed to write output file: " << filePath << "\n";
        return false;
    }
    return true;
}

}  // namespace CcuAgNhr1dMem2mem
