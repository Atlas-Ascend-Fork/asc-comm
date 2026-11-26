/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "test_record.h"
#include "alltoallv_write.h"
#include "options.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace asccomm_api_bench {
namespace {
std::string Quote(const std::string& value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c)
                        << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

std::string Strings(const std::vector<std::string>& values)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << Quote(values[i]);
    }
    out << ']';
    return out.str();
}

std::string Numbers(const std::vector<uint64_t>& values)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << values[i];
    }
    out << ']';
    return out.str();
}

std::string WriteLayout(const AllToAllVWriteLayout& layout)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"pattern\":\"balanced\",\"array_layout\":\"mission_major\",\"mission_count\":2,"
           "\"wire_dtype\":"
        << Quote(layout.wire_dtype) << ",\"per_peer_bytes\":" << layout.per_peer_bytes
        << ",\"send_offsets_bytes\":" << Numbers(layout.send_offsets_bytes)
        << ",\"send_sizes_bytes\":" << Numbers(layout.send_sizes_bytes)
        << ",\"remote_window_offsets_bytes\":" << Numbers(layout.remote_window_offsets_bytes)
        << ",\"local_data_size_bytes\":" << layout.local_data_size_bytes << ",\"window_bytes\":" << layout.window_bytes
        << '}';
    return out.str();
}

std::string Entry(const PlanEntry& entry)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"id\":" << entry.id << ",\"requested_bytes\":" << entry.requested_bytes
        << ",\"algorithm\":" << Quote(entry.algorithm) << ",\"kind\":" << Quote(PlanKindName(entry.kind)) << '}';
    return out.str();
}

bool AllowsMetrics(ResultStatus status)
{
    return status == ResultStatus::Hit || status == ResultStatus::Fallback ||
           status == ResultStatus::AlgorithmUnconfirmed;
}

void ValidateResult(const TestResult& result)
{
    if (result.entry.kind == PlanKind::FallbackAttempt)
        throw std::invalid_argument("schema 2 cannot execute an unmatched algorithm fallback attempt");
    if ((!result.entry.algorithm.empty() || result.entry.kind != PlanKind::Unsupported) &&
        !IsExternalAlgorithmName(result.entry.algorithm)) {
        throw std::invalid_argument("result request must use an external [] algorithm name");
    }
    if (result.shape.requested_bytes != 0 && result.entry.requested_bytes != result.shape.requested_bytes) {
        throw std::invalid_argument("result shape belongs to another requested length");
    }
    if ((result.status == ResultStatus::Hit || result.status == ResultStatus::Fallback) &&
        (!IsExternalAlgorithmName(result.actual_algorithm) || result.evidence.empty() || result.ret != 0)) {
        throw std::invalid_argument("confirmed result requires actual algorithm, evidence and success");
    }
    if (result.status == ResultStatus::Hit && result.actual_algorithm != result.entry.algorithm) {
        throw std::invalid_argument("a hit must confirm the requested algorithm");
    }
    if (result.status == ResultStatus::Fallback && result.actual_algorithm == result.entry.algorithm) {
        throw std::invalid_argument("fallback must identify a different actual algorithm");
    }
    if ((!AllowsMetrics(result.status) || result.ret != 0) && !result.metrics.empty()) {
        throw std::invalid_argument("failed or unexecuted result cannot contain performance values");
    }
    if (result.entry.kind == PlanKind::Unsupported) {
        if (result.status != ResultStatus::Unsupported)
            throw std::invalid_argument("unsupported plan entry must remain unsupported");
        if (!result.actual_algorithm.empty() || !result.evidence.empty())
            throw std::invalid_argument("unsupported result must have no execution evidence");
    }
    std::set<std::string> names;
    for (const Metric& metric : result.metrics) {
        if (metric.name.empty() || !std::isfinite(metric.value) || metric.value < 0 ||
            !names.insert(metric.name).second) {
            throw std::invalid_argument("metric must have a unique name and finite nonnegative value");
        }
    }
}
} // namespace

const char* PlanKindName(PlanKind kind)
{
    switch (kind) {
        case PlanKind::Supported:
            return "supported";
        case PlanKind::FallbackAttempt:
            return "fallback_attempt";
        case PlanKind::Unsupported:
            return "unsupported";
    }
    throw std::invalid_argument("invalid plan kind");
}

const char* ResultStatusName(ResultStatus status)
{
    switch (status) {
        case ResultStatus::Pending:
            return "pending";
        case ResultStatus::Hit:
            return "hit";
        case ResultStatus::Fallback:
            return "fallback";
        case ResultStatus::AlgorithmUnconfirmed:
            return "algorithm_unconfirmed";
        case ResultStatus::Unsupported:
            return "unsupported";
        case ResultStatus::ExecutionFailed:
            return "execution_failed";
        case ResultStatus::ValidationFailed:
            return "validation_failed";
        case ResultStatus::CollectionFailed:
            return "collection_failed";
        case ResultStatus::ParseFailed:
            return "parse_failed";
        case ResultStatus::Unfinished:
            return "unfinished";
    }
    throw std::invalid_argument("invalid result status");
}

RecordWriter::RecordWriter(std::ostream& out, const std::string& job_id) : out_(out), job_id_(job_id)
{
    if (job_id.empty())
        throw std::invalid_argument("job_id must not be empty");
}

void RecordWriter::Write(const std::string& type, const std::string& fields)
{
    out_ << "{\"schema_version\":2,\"type\":" << Quote(type) << ",\"job_id\":" << Quote(job_id_) << ',' << fields
         << "}\n";
    out_.flush();
    if (!out_)
        throw std::runtime_error("failed to write structured benchmark record");
}

void RecordWriter::WriteJob(
    const Options& options, const std::vector<uint64_t>& lengths, const std::map<std::string, std::string>& metadata)
{
    if (job_written_)
        throw std::logic_error("job header was already written");
    if (!std::isfinite(options.factor))
        throw std::invalid_argument("factor must be finite");
    ValidateOptions(options);
    const std::vector<QueryRequest> requests = MakeQueryRequests(options, lengths);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "\"options\":{\"op\":" << Quote(OpName(options.op))
        << ",\"engine\":" << Quote(EngineName(options.engine)) << ",\"ranks\":" << options.ranks
        << ",\"devices_per_node\":" << options.devices_per_node << ",\"dtype\":" << Quote(options.dtype)
        << ",\"reduce\":" << Quote(options.reduce) << ",\"begin\":" << options.begin << ",\"end\":" << options.end
        << ",\"begin_set\":" << (options.begin_set ? "true" : "false")
        << ",\"end_set\":" << (options.end_set ? "true" : "false") << ",\"factor\":" << options.factor
        << ",\"increment\":" << options.increment << ",\"factor_set\":" << (options.factor_set ? "true" : "false")
        << ",\"increment_set\":" << (options.increment_set ? "true" : "false")
        << ",\"algorithms\":" << Strings(options.algorithms) << "},\"lengths\":[";
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (i)
            out << ',';
        out << lengths[i];
    }
    out << "],\"query_mode\":" << Quote(QueryModeName(QueryModeForOptions(options))) << ",\"query_requests\":[";
    for (size_t i = 0; i < requests.size(); ++i) {
        if (i)
            out << ',';
        const QueryRequest& request = requests[i];
        out << "{\"id\":" << request.id << ",\"requested_bytes\":" << request.requested_bytes
            << ",\"data_len\":" << request.data_len << '}';
    }
    out << "],\"metadata\":{";
    bool first = true;
    for (const auto& item : metadata) {
        if (!first)
            out << ',';
        first = false;
        out << Quote(item.first) << ':' << Quote(item.second);
    }
    out << '}';
    Write("job", out.str());
    options_ = options;
    job_written_ = true;
}

void RecordWriter::WriteQuery(const QueryRecord& query)
{
    if (!job_written_)
        throw std::logic_error("write the job header first");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "\"query_id\":" << query.query_id << ",\"query_mode\":" << Quote(QueryModeName(query.query_mode))
        << ",\"requested_bytes\":" << query.requested_bytes << ",\"cc_type\":" << static_cast<unsigned>(query.cc_type)
        << ",\"comm_engine\":" << static_cast<unsigned>(query.comm_engine)
        << ",\"data_len_set\":" << (query.data_len_set ? "true" : "false") << ",\"data_len\":";
    if (query.data_len_set)
        out << query.data_len;
    else
        out << "null";
    out << ",\"ret\":" << query.ret << ",\"release_ret\":" << query.release_ret
        << ",\"error_stage\":" << Quote(query.error_stage) << ",\"algorithms\":" << Strings(query.algorithms);
    if (!query.error.empty())
        out << ",\"error\":" << Quote(query.error);
    Write("query", out.str());
}

void RecordWriter::WritePlan(const PlanEntry& entry)
{
    if (!job_written_)
        throw std::logic_error("write the job header first");
    if (entry.kind == PlanKind::FallbackAttempt)
        throw std::invalid_argument("schema 2 cannot plan an unmatched algorithm fallback attempt");
    if ((!entry.algorithm.empty() || entry.kind != PlanKind::Unsupported) &&
        !IsExternalAlgorithmName(entry.algorithm)) {
        throw std::invalid_argument("plan requires an external [] name; unsupported may use an empty placeholder");
    }
    Write("plan", "\"entry\":" + Entry(entry));
}

void RecordWriter::WriteResult(const TestResult& result)
{
    if (!job_written_)
        throw std::logic_error("write the job header first");
    ValidateResult(result);
    if (options_.op == Op::AllToAllVWrite) {
        if (result.write_layout.enabled || (AllowsMetrics(result.status) && result.ret == 0)) {
            ValidateAllToAllVWriteLayout(options_, result.shape, result.write_layout);
        }
    } else if (result.write_layout.enabled) {
        throw std::invalid_argument("write layout belongs only to alltoallvwrite results");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "\"entry\":" << Entry(result.entry)
        << ",\"shape\":{\"requested_bytes\":" << result.shape.requested_bytes
        << ",\"input_count\":" << result.shape.input_count << ",\"output_count\":" << result.shape.output_count
        << ",\"input_bytes\":" << result.shape.input_bytes << ",\"output_bytes\":" << result.shape.output_bytes
        << ",\"bandwidth_bytes\":" << result.shape.bandwidth_bytes
        << ",\"per_rank_count\":" << result.shape.per_rank_count
        << ",\"per_peer_count\":" << result.shape.per_peer_count
        << "},\"status\":" << Quote(ResultStatusName(result.status))
        << ",\"actual_algorithm\":" << Quote(result.actual_algorithm) << ",\"evidence\":" << Quote(result.evidence)
        << ",\"failure_stage\":" << Quote(result.failure_stage) << ",\"error\":" << Quote(result.error)
        << ",\"ret\":" << result.ret << ",\"batch\":" << result.batch << ",\"metrics\":{";
    for (size_t i = 0; i < result.metrics.size(); ++i) {
        if (i)
            out << ',';
        out << Quote(result.metrics[i].name) << ':' << result.metrics[i].value;
    }
    out << '}';
    if (result.write_layout.enabled)
        out << ",\"write_layout\":" << WriteLayout(result.write_layout);
    Write("result", out.str());
}

void RecordWriter::WritePhase(const PhaseRecord& phase)
{
    if (!job_written_)
        throw std::logic_error("write the job header first");
    static const std::set<std::string> phases = {"initialization", "query", "warmup", "measurement", "validation"};
    if (!phases.count(phase.phase) || (phase.event != "begin" && phase.event != "end") || phase.rank < 0) {
        throw std::invalid_argument("invalid phase record");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "\"phase\":" << Quote(phase.phase) << ",\"event\":" << Quote(phase.event) << ",\"rank\":" << phase.rank
        << ",\"process\":" << phase.process << ",\"entry_id\":" << phase.entry_id << ",\"batch\":" << phase.batch
        << ",\"iteration\":" << phase.iteration << ",\"host_monotonic_ns\":" << phase.host_monotonic_ns;
    Write("phase", out.str());
}

void RecordWriter::WriteFailure(const std::string& stage, int64_t ret, const std::string& error)
{
    if (!job_written_)
        throw std::logic_error("write the job header first");
    if (stage.empty() || error.empty()) {
        throw std::invalid_argument("failure record requires a stage and error description");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "\"stage\":" << Quote(stage) << ",\"ret\":" << ret << ",\"error\":" << Quote(error);
    Write("failure", out.str());
}

} // namespace asccomm_api_bench
