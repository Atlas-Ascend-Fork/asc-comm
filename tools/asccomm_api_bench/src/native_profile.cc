/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "native_profile.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "alltoallv_write.h"

namespace asccomm_api_bench {
namespace {

void Check(int code, const char* api)
{
    if (code != ACL_SUCCESS)
        throw std::runtime_error(std::string(api) + " failed, ret=" + std::to_string(code));
}

std::string Quote(const std::string& value)
{
    std::ostringstream result;
    result << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            result << '\\' << c;
        else if (c < 32)
            result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
        else
            result << c;
    }
    result << '"';
    return result.str();
}

void EmptyDirectory(const std::string& path)
{
    if (path.empty() || (mkdir(path.c_str(), 0750) != 0 && errno != EEXIST))
        throw std::runtime_error("cannot create profiling directory: " + path);
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr)
        throw std::runtime_error("cannot open profiling directory: " + path);
    bool empty = true;
    while (const auto item = readdir(directory)) {
        const std::string name(item->d_name);
        if (name != "." && name != "..")
            empty = false;
    }
    closedir(directory);
    if (!empty)
        throw std::runtime_error("profiling directory must be empty for this rank/run: " + path);
}

void Shape(std::ostream& output, const CaseShape& shape)
{
    output << "{\"requested_bytes\":" << shape.requested_bytes << ",\"input_count\":" << shape.input_count
           << ",\"output_count\":" << shape.output_count << ",\"input_bytes\":" << shape.input_bytes
           << ",\"output_bytes\":" << shape.output_bytes << ",\"bandwidth_bytes\":" << shape.bandwidth_bytes
           << ",\"per_rank_count\":" << shape.per_rank_count << ",\"per_peer_count\":" << shape.per_peer_count << '}';
}

void Numbers(std::ostream& output, const std::vector<uint64_t>& values)
{
    output << '[';
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            output << ',';
        output << values[index];
    }
    output << ']';
}
} // namespace

NativeProfilingSession::NativeProfilingSession(
    const std::string& directory, uint32_t device, const Options& options, uint32_t rank, const std::string& run_id,
    const std::string& job_id, const std::string& measurement_mode)
    : directory_(directory),
      run_id_(run_id),
      job_id_(job_id),
      measurement_mode_(measurement_mode),
      options_(options),
      rank_(rank),
      device_(device)
{
    if ((measurement_mode_ != "full" && measurement_mode_ != "comm_only") ||
        (measurement_mode_ == "comm_only" && options.engine != Engine::AicpuTs))
        throw std::invalid_argument("profiling measurement mode must be full, or comm_only with AICPU");
    EmptyDirectory(directory_);
    const char* soc = aclrtGetSocName();
    if (soc != nullptr)
        soc_name_ = soc;
    std::string normalized = soc_name_;
    for (char& c : normalized)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (options.engine != Engine::AicpuTs && normalized.find("ascend950") != 0)
        throw std::runtime_error("CCU Wait cycle conversion requires the installed Ascend950 package and device");
    try {
        Check(hccl_api_test::StartProfiling(static_cast<int>(device_), profile_, directory_), "legacy StartProfiling");
    } catch (...) {
        Release();
        throw;
    }
}

int NativeProfilingSession::Release() noexcept { return hccl_api_test::StopProfiling(profile_); }

NativeProfilingSession::~NativeProfilingSession() { Release(); }

void NativeProfilingSession::Record(
    const TestResult& result, const NativeDeviceResult& measured, uint32_t warmup, uint32_t iterations)
{
    if (!profile_.started || finished_ || !measured.validated || iterations == 0 ||
        measured.iteration_samples.size() != static_cast<uint64_t>(warmup) + iterations)
        throw std::runtime_error("profiling submission coverage differs from the validated execution");
    uint32_t measured_count = 0, warmup_count = 0;
    for (const auto& sample : measured.iteration_samples) {
        const uint32_t expected = sample.measured ? ++measured_count : warmup_count++;
        if (sample.iteration != expected ||
            (sample.measured && (!std::isfinite(sample.event_us) || sample.event_us <= 0)))
            throw std::runtime_error("profiling submission iteration or Event measurement is invalid");
    }
    if (measured_count != iterations || warmup_count != warmup)
        throw std::runtime_error("profiling submission warmup/measured counts are incomplete");
    Entry entry;
    entry.result = result;
    entry.measured = measured;
    entry.warmup = warmup;
    entry.iterations = iterations;
    entries_.push_back(std::move(entry));
}

void NativeProfilingSession::Finish()
{
    if (finished_)
        return;
    // The AICPU server runs on an attached stream. Its full tail must finish
    // before stopping the collector; synchronizing only the client is insufficient.
    if (profile_.started)
        Check(aclrtSynchronizeDevice(), "aclrtSynchronizeDevice(profiling)");
    Check(Release(), "profiling stop/finalize");
    const std::string temporary = directory_ + "/native_capture.json.tmp";
    std::ofstream output(temporary.c_str(), std::ios::out | std::ios::trunc);
    output.imbue(std::locale::classic());
    output << std::setprecision(17)
           << "{\"schema_version\":1,\"kind\":\"native_profile_capture\",\"run_id\":" << Quote(run_id_)
           << ",\"job_id\":" << Quote(job_id_) << ",\"global_rank\":" << rank_ << ",\"world_size\":" << options_.ranks
           << ",\"backend_pid\":" << getpid() << ",\"logical_device\":" << device_
           << ",\"engine\":" << Quote(EngineName(options_.engine))
           << ",\"measurement_mode\":" << Quote(measurement_mode_) << ",\"soc_name\":"
           << Quote(soc_name_)
           // Ascend950 GetSystemCycle uses the 1 GHz SYS_CNT clock. The
           // constructor rejects other CCU SoCs rather than applying this scale.
           << ",\"cycles_per_us\":" << (options_.engine == Engine::AicpuTs ? 0 : 1000) << ",\"entries\":[";
    for (size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (index != 0)
            output << ',';
        output << "{\"entry_id\":" << entry.result.entry.id << ",\"batch\":" << entry.result.batch
               << ",\"requested_bytes\":" << entry.result.entry.requested_bytes << ",\"shape\":";
        Shape(output, entry.result.shape);
        output << ",\"iterations\":" << entry.iterations << ",\"warmup\":" << entry.warmup;
        if (options_.op == Op::AllToAllVWrite) {
            const auto plan = MakeAllToAllVWritePlan(
                options_, entry.result.entry.requested_bytes, rank_, entry.measured.window_capacity_bytes);
            output << ",\"write_plan\":{\"global_rank\":" << rank_ << ",\"send_offsets_bytes\":";
            Numbers(output, plan.send_offsets_bytes);
            output << ",\"send_sizes_bytes\":";
            Numbers(output, plan.send_sizes_bytes);
            output << ",\"remote_window_offset_bytes\":" << plan.remote_window_offset_bytes
                   << ",\"local_data_size_bytes\":" << plan.local_data_size_bytes
                   << ",\"window_bytes\":" << plan.window_bytes << '}';
        }
        output << ",\"submissions\":[";
        for (size_t item = 0; item < entry.measured.iteration_samples.size(); ++item) {
            const auto& sample = entry.measured.iteration_samples[item];
            if (item != 0)
                output << ',';
            output << "{\"phase\":" << Quote(sample.measured ? "measured" : "warmup")
                   << ",\"iteration\":" << sample.iteration << ",\"stream_id\":" << sample.stream_id
                   << ",\"task_id\":" << sample.task_id;
            if (sample.measured)
                output << ",\"acl_event_us\":" << sample.event_us;
            output << ",\"wait_report_present\":" << (sample.wait_report_present ? "true" : "false");
            if (sample.wait_report_present)
                output << ",\"wait_duration_ns\":" << sample.wait_duration_ns;
            output << '}';
        }
        output << "]}";
    }
    output << "]}\n";
    output.close();
    if (!output || std::rename(temporary.c_str(), (directory_ + "/native_capture.json").c_str()) != 0)
        throw std::runtime_error("cannot publish profiling submission manifest: " + directory_);
    finished_ = true;
}

} // namespace asccomm_api_bench
