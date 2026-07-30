/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_BINARY_STREAM_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_BINARY_STREAM_H

#include <algorithm>
#include <iterator>
#include <sstream>
#include <vector>

class BinaryStream {
public:
    static constexpr std::ios_base::openmode DEFAULT_IOS_MODE =
        std::ios_base::in | std::ios_base::out;

    explicit BinaryStream(std::ios_base::openmode mode = DEFAULT_IOS_MODE)
        : stream_(mode | std::ios_base::binary)
    {
    }

    explicit BinaryStream(
        std::vector<char> &buffer,
        std::ios_base::openmode mode = DEFAULT_IOS_MODE)
        : stream_(mode | std::ios_base::binary)
    {
        stream_.rdbuf()->pubsetbuf(buffer.data(), buffer.size());
    }

    template <typename T>
    BinaryStream &operator<<(const T &value)
    {
        stream_.write(reinterpret_cast<const char *>(&value), sizeof(T));
        return *this;
    }

    template <typename T>
    BinaryStream &operator<<(const std::vector<T> &values)
    {
        const size_t size = values.size();
        *this << size;
        for (const auto &value : values) {
            *this << value;
        }
        return *this;
    }

    template <typename T>
    BinaryStream &operator>>(T &value)
    {
        stream_.read(reinterpret_cast<char *>(&value), sizeof(T));
        return *this;
    }

    template <typename T>
    BinaryStream &operator>>(std::vector<T> &values)
    {
        size_t size = 0;
        *this >> size;
        values.resize(size);
        for (auto &value : values) {
            *this >> value;
        }
        return *this;
    }

    void Dump(std::vector<char> &result)
    {
        std::for_each(
            std::istreambuf_iterator<char>(stream_),
            std::istreambuf_iterator<char>(),
            [&result](char value) { result.push_back(value); });
    }

private:
    std::stringstream stream_;
};

#endif  // ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_BINARY_STREAM_H
