/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "options.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace asccomm_api_bench {
namespace {
constexpr size_t MAX_ALGORITHM_BYTES = 4096;
constexpr size_t MAX_RANGE_STEPS = 1000000;

uint64_t Unsigned(const std::string& text)
{
    if (text.empty())
        throw std::invalid_argument("expected a positive decimal integer");
    uint64_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9')
            throw std::invalid_argument("invalid integer: " + text);
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            throw std::overflow_error("integer overflow: " + text);
        }
        value = value * 10 + digit;
    }
    return value;
}

uint64_t ByteSize(const std::string& text)
{
    size_t end = 0;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9')
        ++end;
    const uint64_t value = Unsigned(text.substr(0, end));
    const std::string suffix = text.substr(end);
    uint64_t multiplier = 1;
    if (suffix.empty() || suffix == "B")
        multiplier = 1;
    else if (suffix == "k" || suffix == "K" || suffix == "KB" || suffix == "KiB")
        multiplier = 1024;
    else if (suffix == "m" || suffix == "M" || suffix == "MB" || suffix == "MiB")
        multiplier = 1048576;
    else if (suffix == "g" || suffix == "G" || suffix == "GB" || suffix == "GiB")
        multiplier = 1073741824;
    else
        throw std::invalid_argument("unknown byte suffix: " + text);
    if (value > std::numeric_limits<uint64_t>::max() / multiplier) {
        throw std::overflow_error("byte size overflow: " + text);
    }
    return value * multiplier;
}

uint32_t RankCount(const std::string& text)
{
    const uint64_t value = Unsigned(text);
    if (value == 0 || value > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("rank/device count must fit a positive MPI int");
    }
    return static_cast<uint32_t>(value);
}

std::string Trim(const std::string& text)
{
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

void AddUnique(std::vector<std::string>& target, const std::string& value)
{
    if (std::find(target.begin(), target.end(), value) == target.end())
        target.push_back(value);
}
} // namespace

const char* OpName(Op op)
{
    switch (op) {
        case Op::AllGather:
            return "allgather";
        case Op::AllReduce:
            return "allreduce";
        case Op::ReduceScatter:
            return "reducescatter";
        case Op::AllToAll:
            return "alltoall";
        case Op::AllToAllV:
            return "alltoallv";
        case Op::AllToAllVWrite:
            return "alltoallvwrite";
    }
    throw std::invalid_argument("invalid operation");
}

const char* EngineName(Engine engine)
{
    switch (engine) {
        case Engine::AicpuTs:
            return "aicpu_ts";
        case Engine::CcuSched:
            return "ccu_sched";
        case Engine::CcuMs:
            return "ccu_ms";
    }
    throw std::invalid_argument("invalid engine");
}

uint8_t CommEngineCode(Engine engine)
{
    switch (engine) {
        case Engine::AicpuTs:
            return 2;
        case Engine::CcuSched:
            return 6;
        case Engine::CcuMs:
            return 5;
    }
    throw std::invalid_argument("invalid engine");
}

void ValidateMeasurementDType(const Options& options)
{
    // Reused policy: run_benchmark_aicpu.sh / run_benchmark_ccu.sh in hccl_api_test.
    static const std::vector<std::string> aicpu = {"int8", "int64", "uint64", "fp16", "fp32", "fp64", "bf16"};
    static const std::vector<std::string> ccu = {"int8", "fp16", "fp32", "bf16"};
    const auto& allowed = options.engine == Engine::AicpuTs ? aicpu : ccu;
    const std::string dtype = options.dtype == "bfp16" ? "bf16" : options.dtype;
    if (std::find(allowed.begin(), allowed.end(), dtype) == allowed.end()) {
        std::string supported;
        for (const auto& name : allowed)
            supported += (supported.empty() ? "" : ",") + name;
        throw std::invalid_argument(
            std::string(EngineName(options.engine)) + " measurements support -d " + supported +
            " (bfp16 aliases bf16)");
    }
}

size_t DTypeSize(const std::string& dtype)
{
    if (dtype == "int8" || dtype == "uint8" || dtype == "hif8" || dtype == "fp8e4m3" || dtype == "fp8e5m2" ||
        dtype == "fp8e8m0")
        return 1;
    if (dtype == "int16" || dtype == "uint16" || dtype == "fp16" || dtype == "bfp16" || dtype == "bf16")
        return 2;
    if (dtype == "int32" || dtype == "uint32" || dtype == "fp32")
        return 4;
    if (dtype == "int64" || dtype == "uint64" || dtype == "fp64")
        return 8;
    throw std::invalid_argument("unsupported dtype: " + dtype);
}

std::vector<std::string> ParseAlgorithms(const std::string& text)
{
    std::vector<std::string> values;
    size_t begin = 0;
    size_t depth = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const bool finish = i == text.size();
        if (!finish) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (ch < 32 || ch == 127)
                throw std::invalid_argument("algorithm contains a control character");
            if (ch == '[')
                ++depth;
            if (ch == ']') {
                if (depth == 0)
                    throw std::invalid_argument("unbalanced algorithm brackets");
                --depth;
            }
        }
        if (finish || (text[i] == ',' && depth == 0)) {
            if (finish && depth != 0)
                throw std::invalid_argument("unbalanced algorithm brackets");
            const std::string value = Trim(text.substr(begin, i - begin));
            if (value.empty())
                throw std::invalid_argument("empty algorithm name");
            if (value.size() > MAX_ALGORITHM_BYTES)
                throw std::invalid_argument("algorithm exceeds 4096 bytes");
            AddUnique(values, value);
            begin = i + 1;
        }
    }
    return values;
}

bool IsExternalAlgorithmName(const std::string& name)
{
    const size_t bracket = name.find('[');
    if (bracket == std::string::npos || bracket == 0 || name.back() != ']')
        return false;
    try {
        const auto values = ParseAlgorithms(name);
        return values.size() == 1 && values.front() == name;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void ValidateOptions(const Options& options)
{
    if (!options.op_set)
        throw std::invalid_argument("--op is required");
    (void)OpName(options.op);
    (void)EngineName(options.engine);
    (void)DTypeSize(options.dtype);
    if (options.op == Op::AllToAllVWrite && options.engine == Engine::AicpuTs) {
        throw std::invalid_argument("alltoallvwrite requires -a ccu_sched");
    }
    if (options.ranks == 0 || options.devices_per_node == 0 ||
        options.ranks > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        options.devices_per_node > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("rank/device count must fit a positive MPI int");
    }
    if (options.reduce != "sum" && options.reduce != "prod" && options.reduce != "max" && options.reduce != "min") {
        throw std::invalid_argument("reduce must be sum, prod, max or min");
    }
    if ((!options.begin_set && options.begin != 32768) || (!options.end_set && options.end != 268435456))
        throw std::invalid_argument("custom size boundaries must preserve explicit begin/end intent");
    if (options.begin == 0 || options.end < options.begin)
        throw std::invalid_argument("require 0 < begin <= end");
    if (options.factor_set && options.increment_set)
        throw std::invalid_argument("-f and -i are mutually exclusive");
    if (options.increment_set) {
        if (options.increment == 0)
            throw std::invalid_argument("-i must be positive");
    } else if (!std::isfinite(options.factor) || options.factor <= 1) {
        throw std::invalid_argument("-f must be finite and greater than 1");
    }
    for (const std::string& algorithm : options.algorithms) {
        if (!IsExternalAlgorithmName(algorithm)) {
            throw std::invalid_argument(
                "algorithms must use individual external [] names; registered names need a formal mapping");
        }
    }
}

Options ParseOptions(const std::vector<std::string>& args)
{
    Options options;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string key = args[i];
        std::string value;
        const size_t equals = key.find('=');
        if (equals != std::string::npos) {
            value = key.substr(equals + 1);
            key.resize(equals);
        } else {
            if (i + 1 == args.size())
                throw std::invalid_argument("missing value for " + key);
            value = args[++i];
        }
        if (key == "--op") {
            if (value == "allgather")
                options.op = Op::AllGather;
            else if (value == "allreduce")
                options.op = Op::AllReduce;
            else if (value == "reducescatter")
                options.op = Op::ReduceScatter;
            else if (value == "alltoall")
                options.op = Op::AllToAll;
            else if (value == "alltoallv")
                options.op = Op::AllToAllV;
            else if (value == "alltoallvwrite")
                options.op = Op::AllToAllVWrite;
            else
                throw std::invalid_argument("unknown operation: " + value);
            options.op_set = true;
        } else if (key == "-n")
            options.ranks = RankCount(value);
        else if (key == "-p")
            options.devices_per_node = RankCount(value);
        else if (key == "-d")
            options.dtype = value == "bfp16" ? "bf16" : value;
        else if (key == "-o")
            options.reduce = value;
        else if (key == "-a") {
            if (value == "aicpu_ts")
                options.engine = Engine::AicpuTs;
            else if (value == "ccu_sched")
                options.engine = Engine::CcuSched;
            else if (value == "ccu_ms")
                options.engine = Engine::CcuMs;
            else
                throw std::invalid_argument("unknown engine: " + value);
        } else if (key == "-b") {
            options.begin = ByteSize(value);
            options.begin_set = true;
        } else if (key == "-e") {
            options.end = ByteSize(value);
            options.end_set = true;
        } else if (key == "-i") {
            options.increment = ByteSize(value);
            options.increment_set = true;
        } else if (key == "-f") {
            char* end = nullptr;
            errno = 0;
            options.factor = std::strtod(value.c_str(), &end);
            if (errno != 0 || end == value.c_str() || *end != '\0' ||
                value.find_first_of(" \t\r\n") != std::string::npos) {
                throw std::invalid_argument("invalid factor: " + value);
            }
            options.factor_set = true;
        } else if (key == "--alg") {
            for (const std::string& algorithm : ParseAlgorithms(value))
                AddUnique(options.algorithms, algorithm);
        } else
            throw std::invalid_argument("unknown argument: " + key);
    }
    ValidateOptions(options);
    return options;
}

std::vector<uint64_t> ExpandLengths(const Options& options)
{
    ValidateOptions(options);
    std::vector<uint64_t> lengths(1, options.begin);
    if (options.begin == options.end)
        return lengths;
    if (options.increment_set) {
        uint64_t current = options.begin;
        while (options.end - current >= options.increment) {
            if (lengths.size() >= MAX_RANGE_STEPS)
                throw std::invalid_argument("range exceeds one million points");
            current += options.increment; // bounded by end; cannot overflow
            lengths.push_back(current);
        }
        return lengths;
    }
    const long double uint64_limit = std::ldexp(1.0L, 64);
    if (std::floor(options.factor) == options.factor) {
        if (static_cast<long double>(options.factor) >= uint64_limit)
            throw std::overflow_error("multiplicative range overflow");
        const uint64_t factor = static_cast<uint64_t>(options.factor);
        uint64_t current = options.begin;
        while (current < options.end) {
            if (lengths.size() >= MAX_RANGE_STEPS)
                throw std::invalid_argument("range exceeds one million points");
            if (current > std::numeric_limits<uint64_t>::max() / factor)
                throw std::overflow_error("multiplicative range overflow");
            current *= factor;
            lengths.push_back(current);
        }
        return lengths;
    }
    long double raw = options.begin;
    for (size_t step = 1; step < MAX_RANGE_STEPS; ++step) {
        const long double next = raw * static_cast<long double>(options.factor);
        if (!std::isfinite(next) || next <= raw || std::ceil(next) >= uint64_limit) {
            throw std::overflow_error("multiplicative range cannot safely generate next byte length");
        }
        raw = next;
        const uint64_t point = static_cast<uint64_t>(std::ceil(raw));
        if (point != lengths.back())
            lengths.push_back(point);
        if (point >= options.end)
            return lengths;
    }
    throw std::invalid_argument("range exceeds one million expansion steps");
}

const char* QueryModeName(QueryMode mode)
{
    switch (mode) {
        case QueryMode::TopologyAll:
            return "topology_all";
        case QueryMode::SinglePoint:
            return "single_point";
        case QueryMode::RangeUnion:
            return "range_union";
    }
    throw std::invalid_argument("invalid query mode");
}

QueryMode QueryModeForOptions(const Options& options)
{
    ValidateOptions(options);
    if (!options.algorithms.empty() || (!options.begin_set && !options.end_set))
        return QueryMode::TopologyAll;
    return options.begin == options.end ? QueryMode::SinglePoint : QueryMode::RangeUnion;
}

std::vector<QueryRequest> MakeQueryRequests(const Options& options, const std::vector<uint64_t>& lengths)
{
    if (lengths != ExpandLengths(options))
        throw std::invalid_argument("query lengths do not match request expansion");
    const QueryMode mode = QueryModeForOptions(options);
    std::vector<QueryRequest> requests;
    if (mode == QueryMode::TopologyAll) {
        QueryRequest request;
        requests.push_back(request);
    } else {
        for (uint64_t length : lengths) {
            QueryRequest request;
            request.id = requests.size();
            request.query_mode = mode;
            request.requested_bytes = length;
            request.data_len = length;
            requests.push_back(request);
        }
    }
    return requests;
}

const char* Usage()
{
    return "--op allgather|allreduce|reducescatter|alltoall|alltoallv|alltoallvwrite (required)\n"
           "-n total_ranks (8)  -p devices_per_node (8)  -d dtype (fp16)\n"
           "-o sum|prod|max|min (sum)  -a aicpu_ts|ccu_sched (aicpu_ts)\n"
           "Measurement types: AICPU int8,int64,uint64,fp16,fp32,fp64,bf16; CCU int8,fp16,fp32,bf16.\n"
           "bfp16 aliases bf16; query-only accepts the wider SDK data types.\n"
           "alltoallvwrite requires CCU; its payload/output follow the legacy tensor layout.\n"
           "-b begin_bytes (32768)  -e target_end_bytes (268435456)\n"
           "-f factor (>1, default 2) OR -i linear_byte_step\n"
           "--alg 'sole[mesh]' (repeatable; top-level comma lists accepted)\n"
           "Named algorithms query dataLen=0; unsupported names are skipped.\n"
           "Without --alg: omitted sizes query 0, a point queries its bytes, ranges query each point then union.\n"
           "Sizes accept B/K/KB/KiB/M/MB/MiB/G/GB/GiB (binary multiples).\n"
           "Multiplicative scans include first point >= target; linear scans stop <= target.\n"
           "Fractional factors use ceil(begin*factor^k) bytes, with duplicate points removed.\n"
           "At most one million expansion steps; algorithm names at most 4096 bytes.\n";
}

} // namespace asccomm_api_bench
