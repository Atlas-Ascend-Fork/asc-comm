/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "native_query.h"

#include <stdexcept>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>
#include <string>

#include "hccl_mc2.h"
#include "options.h"

namespace asccomm_api_bench {
namespace {

static_assert(HCCL_SUCCESS == 0, "query lifecycle requires HCCL_SUCCESS == 0");
using NativeQuery = Mc2Result (*)(HcclComm, uint8_t, void*, char*, uint32_t);
static_assert(
    std::is_same<decltype(&Mc2GetSupportAlgList), NativeQuery>::value,
    "Mc2GetSupportAlgList must have the five-parameter C ABI JSON signature");
using NativeDataLenSetter = Mc2Result (*)(void*, uint64_t, uint64_t);
static_assert(
    std::is_same<decltype(&Mc2SetCcDataLenRange), NativeDataLenSetter>::value,
    "Mc2SetCcDataLenRange must accept uint64_t per-rank byte endpoints");

struct QueryReply {
    uint64_t count = 0;
    std::vector<std::string> names;
    std::vector<std::string> errors;
};

// The SDK contract has exactly three fields and emits strings verbatim without
// JSON escaping. Read that bounded format without splitting names on commas.
class QueryJson {
public:
    explicit QueryJson(const std::string& text) : text_(text) {}
    QueryReply Read()
    {
        QueryReply reply;
        bool count = false, names = false, errors = false;
        Expect('{');
        for (unsigned field = 0; field != 3; ++field) {
            if (field)
                Expect(',');
            const std::string name = String();
            Expect(':');
            if (name == "count" && !count) {
                count = true;
                reply.count = Number();
            } else if (name == "alg_list" && !names) {
                names = true;
                reply.names = Strings();
            } else if (name == "error_msg" && !errors) {
                errors = true;
                reply.errors = Strings();
            } else {
                Invalid();
            }
        }
        Expect('}');
        Space();
        if (position_ != text_.size() || !count || !names || !errors || reply.count != reply.names.size() ||
            (!reply.errors.empty() && reply.count != 0))
            Invalid();
        return reply;
    }

private:
    const std::string& text_;
    size_t position_ = 0;
    void Invalid() const { throw std::runtime_error("Mc2GetSupportAlgList returned invalid query JSON"); }
    void Space()
    {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\n' || text_[position_] == '\r'))
            ++position_;
    }
    bool Take(char expected)
    {
        Space();
        if (position_ == text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }
    void Expect(char expected)
    {
        if (!Take(expected))
            Invalid();
    }
    std::string String()
    {
        Expect('"');
        const size_t begin = position_;
        while (position_ < text_.size() && text_[position_] != '"') {
            if (static_cast<unsigned char>(text_[position_]) < 0x20 || text_[position_] == '\\')
                Invalid();
            ++position_;
        }
        if (position_ == text_.size())
            Invalid();
        const std::string result = text_.substr(begin, position_ - begin);
        ++position_;
        return result;
    }
    uint64_t Number()
    {
        Space();
        const size_t begin = position_;
        uint64_t value = 0;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            const unsigned digit = static_cast<unsigned>(text_[position_++] - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                Invalid();
            value = value * 10 + digit;
        }
        if (begin == position_ || (text_[begin] == '0' && position_ - begin != 1))
            Invalid();
        return value;
    }
    std::vector<std::string> Strings()
    {
        std::vector<std::string> result;
        Expect('[');
        if (!Take(']')) {
            do {
                result.push_back(String());
            } while (Take(','));
            Expect(']');
        }
        return result;
    }
};

uint32_t GetNativeAlgorithms(void* comm, uint8_t cc_type, void* args, std::vector<std::string>& algorithms)
{
    // Use the SDK-recommended 16 KiB buffer. A failed call never publishes a
    // partial list; its numeric code remains authoritative if JSON is absent.
    std::vector<char> buffer(16 * 1024, '\x7f');
    buffer[0] = '\0';
    const uint32_t ret = Mc2GetSupportAlgList(
        static_cast<HcclComm>(comm), cc_type, args, buffer.data(), static_cast<uint32_t>(buffer.size()));
    const char* end = static_cast<const char*>(std::memchr(buffer.data(), '\0', buffer.size()));
    QueryReply reply;
    try {
        if (end == nullptr)
            throw std::runtime_error("Mc2GetSupportAlgList returned a buffer without a NUL terminator");
        const std::string json(buffer.data(), static_cast<size_t>(end - buffer.data()));
        reply = QueryJson(json).Read();
    } catch (const std::exception& error) {
        if (ret != 0)
            throw QueryProviderError(ret, error.what());
        throw;
    }
    std::string diagnostic;
    for (const auto& error : reply.errors) {
        if (!diagnostic.empty())
            diagnostic += "; ";
        diagnostic += error;
    }
    if (ret != 0)
        throw QueryProviderError(ret, diagnostic.empty() ? "MC2 query failed without an error_msg" : diagnostic);
    if (!reply.errors.empty())
        throw std::runtime_error("MC2 query returned success with error_msg: " + diagnostic);
    algorithms.swap(reply.names);
    return ret;
}

uint8_t NativeOp(Op op)
{
    switch (op) {
        case Op::AllGather:
            return static_cast<uint8_t>(HCCL_CMD_ALLGATHER);
        case Op::AllReduce:
            return static_cast<uint8_t>(HCCL_CMD_ALLREDUCE);
        case Op::ReduceScatter:
            return static_cast<uint8_t>(HCCL_CMD_REDUCE_SCATTER);
        case Op::AllToAll:
            return static_cast<uint8_t>(HCCL_CMD_ALLTOALL);
        case Op::AllToAllV:
            return static_cast<uint8_t>(HCCL_CMD_ALLTOALLV);
        case Op::AllToAllVWrite:
            return static_cast<uint8_t>(HCCL_CMD_HALF_ALLTOALLV);
    }
    throw std::invalid_argument("invalid operator for query");
}

uint8_t NativeDType(const std::string& dtype)
{
    if (dtype == "int8")
        return HCCL_DATA_TYPE_INT8;
    if (dtype == "int16")
        return HCCL_DATA_TYPE_INT16;
    if (dtype == "int32")
        return HCCL_DATA_TYPE_INT32;
    if (dtype == "int64")
        return HCCL_DATA_TYPE_INT64;
    if (dtype == "uint8")
        return HCCL_DATA_TYPE_UINT8;
    if (dtype == "uint16")
        return HCCL_DATA_TYPE_UINT16;
    if (dtype == "uint32")
        return HCCL_DATA_TYPE_UINT32;
    if (dtype == "uint64")
        return HCCL_DATA_TYPE_UINT64;
    if (dtype == "fp16")
        return HCCL_DATA_TYPE_FP16;
    if (dtype == "fp32")
        return HCCL_DATA_TYPE_FP32;
    if (dtype == "fp64")
        return HCCL_DATA_TYPE_FP64;
    if (dtype == "bf16" || dtype == "bfp16")
        return HCCL_DATA_TYPE_BFP16;
    if (dtype == "hif8")
        return HCCL_DATA_TYPE_HIF8;
    if (dtype == "fp8e4m3")
        return HCCL_DATA_TYPE_FP8E4M3;
    if (dtype == "fp8e5m2")
        return HCCL_DATA_TYPE_FP8E5M2;
    if (dtype == "fp8e8m0")
        return HCCL_DATA_TYPE_FP8E8M0;
    throw std::invalid_argument("unsupported query dtype: " + dtype);
}

uint8_t NativeReduce(const std::string& reduce)
{
    if (reduce == "sum")
        return HCCL_REDUCE_SUM;
    if (reduce == "prod")
        return HCCL_REDUCE_PROD;
    if (reduce == "max")
        return HCCL_REDUCE_MAX;
    if (reduce == "min")
        return HCCL_REDUCE_MIN;
    throw std::invalid_argument("unsupported query reduction: " + reduce);
}

} // namespace

QueryApi MakeNativeQueryApi()
{
    QueryApi api;
    api.create = &Mc2GetCcArgs;
    api.release = &Mc2FreeCcArgs;
    api.set_engine = &Mc2SetCcCommEngine;
    api.set_src_type = &Mc2SetCcSrcDataType;
    api.set_dst_type = &Mc2SetCcDstDataType;
    api.set_reduce_type = &Mc2SetCcReduceType;
    api.set_data_len = [](void* args, uint64_t bytes) -> uint32_t { return Mc2SetCcDataLenRange(args, bytes, bytes); };
    api.get_algorithms = &GetNativeAlgorithms;
    return api;
}

QueryInput MakeNativeQueryInput(const Options& options, uint64_t requested_bytes)
{
    QueryRequest request;
    request.requested_bytes = requested_bytes;
    request.data_len = requested_bytes;
    request.query_mode = QueryModeForOptions(options);
    return MakeNativeQueryInput(options, request);
}

QueryInput MakeNativeQueryInput(const Options& options, const QueryRequest& request)
{
    ValidateOptions(options);
    (void)QueryModeName(request.query_mode);
    if (request.query_mode != QueryModeForOptions(options))
        throw std::invalid_argument("query mode does not match the user algorithm/size selection policy");
    if (request.query_mode != QueryMode::RangeUnion && request.id != 0)
        throw std::invalid_argument("a topology or single-point query must use request id zero");
    if (request.query_mode == QueryMode::SinglePoint && request.requested_bytes != options.begin)
        throw std::invalid_argument("single-point query bytes do not match the explicit measurement point");
    if (request.requested_bytes != request.data_len ||
        ((request.query_mode == QueryMode::TopologyAll) != (request.data_len == 0))) {
        throw std::invalid_argument("query request mode/bytes are inconsistent");
    }
    QueryInput input;
    input.query_id = request.id;
    input.query_mode = request.query_mode;
    input.requested_bytes = request.requested_bytes;
    input.cc_type = NativeOp(options.op);
    input.comm_engine = CommEngineCode(options.engine);
    input.src_type = NativeDType(options.dtype);
    input.dst_type = input.src_type;
    // ELEMENT length clauses divide dataLen by the source dtype size. Keep
    // source/destination/reduction consistent with execution; a candidate list
    // alone still does not prove complete dtype/reduction support or execution.
    input.set_reduce = options.op == Op::AllReduce || options.op == Op::ReduceScatter;
    if (input.set_reduce)
        input.reduce_type = NativeReduce(options.reduce);
    input.data_len_set = true;
    input.require_data_len = request.query_mode != QueryMode::TopologyAll;
    input.data_len = request.data_len;
    return input;
}

} // namespace asccomm_api_bench
