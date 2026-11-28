/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "case_shape.h"
#include "mock_query.h"
#include "mpi_context.h"
#include "mpi_coordinator.h"
#include "native_comm.h"
#ifdef MC2_BENCH_QUERY_ONLY
#include <acl/acl.h>
#else
#include "native_device.h"
#endif
#include "options.h"
#include "test_record.h"
#ifdef MC2_BENCH_NATIVE_PROFILING
#include "native_profile.h"
#endif
#ifdef MC2_BENCH_NATIVE_QUERY
#include "native_query.h"
#endif
#ifdef MC2_BENCH_CCU_BACKEND
#include "alltoallv_write.h"
#endif

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace asccomm_api_bench {
namespace {
struct BackendOptions {
    Options options;
    bool query_stub = false;
#ifdef MC2_BENCH_NATIVE_PROFILING
    bool profiling = true;
#else
    bool profiling = false;
#endif
    MockQueryScenario scenario = MockQueryScenario::Basic;
    uint32_t warmup = 10;
    uint32_t iterations = 20;
    std::string records;
    std::string measurement_mode = "full";
};

std::string Environment(const char* key)
{
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
}

uint32_t IterationCount(const std::string& text, bool allow_zero)
{
    uint64_t result = 0;
    if (text.empty())
        throw std::invalid_argument("empty iteration count");
    for (char c : text) {
        if (c < '0' || c > '9' || result > (std::numeric_limits<uint32_t>::max() - (c - '0')) / 10)
            throw std::invalid_argument("invalid iteration count: " + text);
        result = result * 10 + static_cast<unsigned>(c - '0');
    }
    if (!allow_zero && result == 0)
        throw std::invalid_argument("iterations must be positive");
    return static_cast<uint32_t>(result);
}

BackendOptions ParseBackend(int argc, char** argv)
{
    BackendOptions result;
    const std::string measurement_mode = Environment("MC2_BENCH_MEASUREMENT_MODE");
    if (!measurement_mode.empty())
        result.measurement_mode = measurement_mode;
    if (result.measurement_mode != "full" && result.measurement_mode != "comm_only")
        throw std::invalid_argument("MC2_BENCH_MEASUREMENT_MODE must be full or comm_only");
    const std::string collect = Environment("MC2_BENCH_NATIVE_PROFILING");
    if (!collect.empty()) {
        if (collect != "0" && collect != "1")
            throw std::invalid_argument("MC2_BENCH_NATIVE_PROFILING must be 0 or 1");
        result.profiling = collect == "1";
    }
#ifndef MC2_BENCH_NATIVE_PROFILING
    if (result.profiling)
        throw std::invalid_argument("this backend was built without native profiling; rebuild with profiling enabled");
#endif
    std::vector<std::string> arguments;
    std::set<std::string> seen;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        const auto equal = argument.find('=');
        const std::string key = argument.substr(0, equal);
        if (key != "--query-stub" && key != "--warmup" && key != "--iterations" && key != "--records") {
            arguments.push_back(argument);
            continue;
        }
        if (!seen.insert(key).second)
            throw std::invalid_argument("duplicate backend option " + key);
        std::string value;
        if (equal != std::string::npos)
            value = argument.substr(equal + 1);
        else {
            if (++index == argc)
                throw std::invalid_argument("missing value for " + key);
            value = argv[index];
        }
        if (key == "--query-stub") {
            result.query_stub = true;
            result.scenario = ParseMockQueryScenario(value);
        } else if (key == "--warmup")
            result.warmup = IterationCount(value, true);
        else if (key == "--iterations")
            result.iterations = IterationCount(value, false);
        else {
            if (value.empty())
                throw std::invalid_argument("--records must not be empty");
            result.records = value;
        }
    }
#ifndef MC2_BENCH_NATIVE_QUERY
    if (!result.query_stub)
        throw std::invalid_argument("this backend was built without formal MC2 query support; rebuild for native "
                                    "queries or use an explicit --query-stub");
#endif
    result.options = ParseOptions(arguments);
#ifdef MC2_BENCH_QUERY_ONLY
    if (seen.count("--warmup") || seen.count("--iterations"))
        throw std::invalid_argument("query-only mode does not accept warmup or measurement iterations");
#else
    ValidateNativeDeviceOptions(result.options);
#endif
    if (result.measurement_mode == "comm_only" && result.options.engine != Engine::AicpuTs)
        throw std::invalid_argument("comm_only measurement mode requires the AICPU engine");
    if (result.query_stub && result.scenario == MockQueryScenario::RankMismatch && result.options.ranks < 2)
        throw std::invalid_argument("rank-mismatch requires at least two ranks");
    if (result.records.empty()) {
        const std::string directory = Environment("MC2_BENCH_RANK_DIR");
        result.records = directory.empty() ? "records.jsonl" : directory + "/records.jsonl";
    }
    return result;
}

void CheckMpi(int ret, const char* operation)
{
    if (ret != MPI_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
}

void CheckAcl(aclError ret, const char* operation)
{
    if (ret != ACL_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
}

// Abort transport errors at the call boundary. In particular, do not unwind a
// bootstrap's locally-created HCCL communicator before terminating the job.
class AbortTransportCoordinator final : public Coordinator {
public:
    explicit AbortTransportCoordinator(MpiCoordinator& implementation) : implementation_(implementation) {}
    uint32_t Rank() const override { return implementation_.Rank(); }
    uint32_t Size() const override { return implementation_.Size(); }
    bool AllTrue(bool value) override
    {
        try {
            return implementation_.AllTrue(value);
        } catch (...) {
            Abort();
        }
    }
    std::string Broadcast(const std::string& value) override
    {
        try {
            return implementation_.Broadcast(value);
        } catch (...) {
            Abort();
        }
    }

private:
    [[noreturn]] void Abort()
    {
        std::cerr << "Fatal MPI control transport failure rank=" << Rank() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        std::_Exit(1);
    }
    MpiCoordinator& implementation_;
};

void OpenNew(std::ofstream& out, const std::string& path)
{
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0)
        throw std::runtime_error("cannot create new output " + path + ": " + std::strerror(errno));
    if (close(descriptor) != 0)
        throw std::runtime_error("failed to close output reservation: " + path);
    out.open(path.c_str(), std::ios::out);
    out.imbue(std::locale::classic());
    if (!out)
        throw std::runtime_error("cannot open output " + path);
}

// Algorithm names remain in RecordWriter's JSON-escaped job/plan records and
// are associated through entry_id. Wait durations use the legacy nanosecond unit.
#ifndef MC2_BENCH_QUERY_ONLY
void WriteSample(
    std::ofstream& output, bool& first, const PlanEntry& entry, const NativeDeviceResult& sample, uint32_t batch)
{
    if (!first)
        output << ',';
    first = false;
    output << "{\"entry_id\":" << entry.id << ",\"batch\":" << batch << ",\"requested_bytes\":" << entry.requested_bytes
           << ",\"acl_event_mean_us\":" << std::setprecision(17) << sample.event_us
           << ",\"validated\":" << (sample.validated ? "true" : "false") << ",\"algorithm_log_scope\":{\"begin\":\""
           << sample.algorithm_log_begin << "\",\"end\":\"" << sample.algorithm_log_end << "\"}";
    if (sample.write_report_present)
        output << ",\"write_report\":{\"scope\":\"last_measured_iteration\",\"wait_duration_ns\":"
               << sample.wait_duration_ns << "},\"window_capacity_bytes\":" << sample.window_capacity_bytes
               << ",\"window_verified_bytes\":" << sample.window_verified_bytes;
    output << '}';
    output.flush();
    if (!output)
        throw std::runtime_error("failed to write native sample");
}

#endif

void PrintAlgorithms(const std::string& label, const std::set<std::string>& names)
{
    std::cout << label;
    if (names.empty())
        std::cout << "无";
    for (auto iterator = names.begin(); iterator != names.end(); ++iterator) {
        if (iterator != names.begin())
            std::cout << ", ";
        std::cout << *iterator;
    }
    std::cout << '\n';
}

void PrintAlgorithmSelection(const Options& options, const QueryPlanResult& result)
{
    const std::set<std::string> requested(options.algorithms.begin(), options.algorithms.end());
    std::set<std::string> candidates, matched, unsupported;
    for (const auto& query : result.queries)
        candidates.insert(query.algorithms.begin(), query.algorithms.end());
    for (const auto& algorithm : requested) {
        if (candidates.count(algorithm))
            matched.insert(algorithm);
        else
            unsupported.insert(algorithm);
    }
    std::cout << "算法选择汇总：算子=" << OpName(options.op) << " 引擎=" << EngineName(options.engine)
              << " Rank=" << options.ranks << '\n';
    if (requested.empty())
        std::cout << "  用户指定：未指定，使用全部候选\n";
    else
        PrintAlgorithms("  用户指定：", requested);
    PrintAlgorithms("  候选算法：", candidates);
#ifdef MC2_BENCH_QUERY_ONLY
    PrintAlgorithms("  匹配算法：", requested.empty() ? candidates : matched);
#else
    PrintAlgorithms("  进入实测：", requested.empty() ? candidates : matched);
#endif
    PrintAlgorithms("  未命中项：", unsupported);
    std::cout << std::flush;
}

int RunBackend(const BackendOptions& backend, Coordinator& coordinator)
{
    // Match the legacy runner's defaults while preserving explicit user values.
    for (const auto& value : std::vector<std::pair<const char*, const char*>>{
             {"HCCL_OP_RETRY_ENABLE", "L0:0, L1:0, L2:0"},
             {"ASCEND_SLOG_PRINT_TO_STDOUT", "0"},
             {"ASCEND_GLOBAL_LOG_LEVEL", "3"}}) {
        if (setenv(value.first, value.second, 0) != 0)
            throw std::runtime_error(std::string("cannot set benchmark environment: ") + value.first);
    }
    const Options& options = backend.options;
    const RankContext context = DiscoverMpiContext(MPI_COMM_WORLD, options);
    const auto lengths = ExpandLengths(options);
    const std::string query_source = backend.query_stub ? "mock" : "native_mc2";
#ifdef MC2_BENCH_QUERY_ONLY
    const std::string run_mode = "query_only";
#else
    const std::string run_mode = "benchmark";
#endif
    const std::string signature = run_mode + ':' + query_source + ':' +
                                  (backend.query_stub ? MockQueryScenarioName(backend.scenario) : "") + ':' +
                                  std::to_string(backend.warmup) + ':' + std::to_string(backend.iterations) + ':' +
                                  (backend.profiling ? "profiling" : "event") + ':' + backend.measurement_mode;
    if (!coordinator.AllTrue(coordinator.Broadcast(signature) == signature))
        throw std::runtime_error(
            "ranks disagree on run mode, query source, stub, iteration counts or measurement mode");

    std::ofstream records;
    std::unique_ptr<RecordWriter> writer;
    std::string job_id = Environment("MC2_BENCH_RUN_ID");
    if (job_id.empty())
        job_id = "native-" + query_source + '-' + std::to_string(static_cast<unsigned long long>(getpid()));
    job_id = coordinator.Broadcast(job_id);
    if (context.global_rank == 0) {
        OpenNew(records, backend.records);
        writer.reset(new RecordWriter(records, job_id));
        std::map<std::string, std::string> metadata = {
            {"query_source", query_source},
            {"execution_source", "native_acl_aclnn"},
            {"timing_source", "acl_event"},
            {"timing_scope", "custom_op_event_rank0_mean"},
            {"warmup", std::to_string(backend.warmup)},
            {"iterations", std::to_string(backend.iterations)},
            {"measurement_iterations", std::to_string(backend.iterations)},
            {"iteration_alignment", "legacy_mpi_notify_gate"},
            {"execution_module", "hccl_api_test"},
            {"execution_baseline", "138ac18b6c496ca3faebab0fa8e8038757ec19a7"},
            {"executor_reuse", options.engine == Engine::CcuSched ? "repeatable" : "single_use"},
            {"validation_scope", "last_measured_output"},
            {"payload_memory_policy", "huge_only"},
            {"skew_retry_policy", "disabled"},
            {"measurement_mode", backend.measurement_mode},
            {"commit_delay_nops", backend.measurement_mode == "comm_only" ? "5000000" : "0"},
            {"profiling_index", backend.profiling ? "native_capture" : "not_collected"}};
#ifdef MC2_BENCH_QUERY_ONLY
        metadata = {
            {"run_mode", "query_only"},
            {"query_source", query_source},
            {"execution_source", "none"},
            {"profiling_index", "not_collected"}};
#endif
        if (backend.query_stub)
            metadata["query_stub"] = MockQueryScenarioName(backend.scenario);
#ifndef MC2_BENCH_QUERY_ONLY
        if (options.op == Op::AllToAllVWrite) {
            metadata["write_event_scope"] = "kernel_including_receive_copy";
            metadata["write_payload_policy"] = "legacy_rank_pattern_slots_aligned_512";
            metadata["write_wire_dtype"] = options.dtype;
        }
#endif
        writer->WriteJob(options, lengths, metadata);
        std::cout << "Query source: ";
        if (backend.query_stub)
            std::cout << "MOCK (" << MockQueryScenarioName(backend.scenario) << ')';
        else
            std::cout << "NATIVE MC2 (Mc2GetSupportAlgList)";
#ifdef MC2_BENCH_QUERY_ONLY
        std::cout << "; mode: query only; no operator execution or profiling\n";
        std::cout << "Query configuration: op=" << OpName(options.op) << " engine=" << EngineName(options.engine)
                  << " dtype=" << options.dtype << " ranks=" << options.ranks << '\n';
#else
        std::cout << "; execution: real ACL/ACLNN on NPU\n";
        std::cout << "Benchmark configuration: op=" << OpName(options.op) << " engine=" << EngineName(options.engine)
                  << " dtype=" << options.dtype << " ranks=" << options.ranks
                  << " devices_per_node=" << options.devices_per_node << " warmup=" << backend.warmup
                  << " iterations=" << backend.iterations << " measurement_points=" << lengths.size()
                  << " first_bytes=" << lengths.front() << " last_bytes=" << lengths.back() << '\n';
        std::cout << "Timing scope: api_kernel_us is the whole custom-op ACL Event; rank 0 iteration mean, in us.\n";
        std::cout << "Iteration alignment: MPI readiness/submission/completion barriers and device Notify gate.\n";
        std::cout << "Measurement mode: " << backend.measurement_mode << '\n';
        if (backend.measurement_mode == "comm_only")
            std::cout << "Delayed Commit diagnosis: raw Event includes artificial delay; "
                         "expansion/kernel report columns are NA.\n";
        std::cout << "Native profiling: "
                  << (backend.profiling ? "enabled; automatic phase analysis" : "disabled; ACL Event only") << '\n';
#endif
    }

    // Native discovery needs a real communicator; explicit query fixtures can
    // still skip before device initialization when no candidate is executable.
    aclrtStream stream = nullptr;
    CommBootstrapResult communication;
#ifndef MC2_BENCH_QUERY_ONLY
    std::unique_ptr<NativeDeviceSession> device_session;
    std::ofstream samples;
    bool first_sample = true;
#endif
    uint32_t device = 0;
    bool initialized = false;
    std::string stage = "device_initialization";
    TestResult active;
    bool active_entry = false;
#ifdef MC2_BENCH_NATIVE_PROFILING
    std::unique_ptr<NativeProfilingSession> profiling;
    const auto finish_profiling = [&] {
        if (profiling) {
            stage = "profiling_finalization";
            profiling->Finish();
            if (writer)
                std::cout << "Native profiling collection finalized; submission manifest ready.\n";
        }
    };
#else
    const auto finish_profiling = [] {};
#endif
    const auto cleanup = [&] {
        if (!initialized)
            return;
        stage = "cleanup";
#ifndef MC2_BENCH_QUERY_ONLY
        device_session.reset();
#endif
        if (stream)
            CheckAcl(aclrtSynchronizeStream(stream), "aclrtSynchronizeStream(cleanup)");
        const int64_t close_result = communication.communicator.Close();
        if (close_result != 0)
            throw std::runtime_error("HcclCommDestroy failed, ret=" + std::to_string(close_result));
        if (stream)
            CheckAcl(aclrtDestroyStream(stream), "aclrtDestroyStream");
        stream = nullptr;
        CheckAcl(aclrtResetDevice(static_cast<int32_t>(device)), "aclrtResetDevice");
        CheckAcl(aclFinalize(), "aclFinalize");
        initialized = false;
    };
    try {
        const auto initialize = [&] {
            if (writer)
                std::cout << "Initializing ACL runtime and NPU devices...\n";
            CheckAcl(aclInit(nullptr), "aclInit");
            uint32_t available = 0;
            CheckAcl(aclrtGetDeviceCount(&available), "aclrtGetDeviceCount");
            device = SelectLocalDevice(context.local_rank, context.local_size, available);
            CheckAcl(aclrtSetDevice(static_cast<int32_t>(device)), "aclrtSetDevice");
#ifndef MC2_BENCH_QUERY_ONLY
            CheckAcl(aclrtCreateStream(&stream), "aclrtCreateStream");
#endif
            std::cout << "Device ready: rank=" << context.global_rank << " local_rank=" << context.local_rank
                      << " device=" << device << " host=" << context.hostname << '\n';
#ifdef MC2_BENCH_NATIVE_PROFILING
            if (backend.profiling) {
                stage = "profiling_initialization";
                std::string directory = Environment("MC2_BENCH_PROFILING_DIR");
                if (directory.empty()) {
                    const std::string rank_directory = Environment("MC2_BENCH_RANK_DIR");
                    directory = rank_directory.empty() ?
                                    "native_profiling_rank_" + std::to_string(context.global_rank) + '_' +
                                        std::to_string(getpid()) :
                                    rank_directory + "/profiling";
                }
                const std::string run_id = Environment("MC2_BENCH_RUN_ID");
                profiling.reset(new NativeProfilingSession(
                    directory, device, options, context.global_rank, run_id.empty() ? job_id : run_id, job_id,
                    backend.measurement_mode));
                std::cout << "Native profiling started: rank=" << context.global_rank << " directory=" << directory
                          << '\n';
                stage = "communicator_initialization";
            }
#endif
            if (writer)
                std::cout << "Initializing HCCL communicator: engine=" << EngineName(options.engine)
                          << " ranks=" << options.ranks << "...\n";
            communication = BootstrapCommunicator(options, coordinator, MakeNativeCommApi());
            if (!communication.Ok())
                throw std::runtime_error(communication.failure_stage + ": " + communication.error);
            if (writer)
                std::cout << "HCCL communicator ready.\n";
            initialized = true;
        };
        if (!backend.query_stub)
            initialize();
        stage = "query";
#ifdef MC2_BENCH_QUERY_ONLY
        const auto plan = QueryCandidates(
#else
        const auto plan = QueryAndPlan(
#endif
            options,
            [&](const QueryRequest& request) {
                if (writer)
                    std::cout << "Query request: id=" << request.id << " mode=" << QueryModeName(request.query_mode)
                              << " dataLen=" << request.data_len << " bytes\n";
                QueryRecord result;
                if (backend.query_stub)
                    result = QueryMockAlgorithms(options, request, backend.scenario, context.global_rank);
#ifdef MC2_BENCH_NATIVE_QUERY
                else
                    result = QueryAlgorithms(
                        communication.communicator.Get(), MakeNativeQueryInput(options, request), MakeNativeQueryApi());
#endif
                if (writer) {
                    if (options.algorithms.empty())
                        std::cout << "用户指定算法：未指定，使用全部候选\n";
                    else
                        PrintAlgorithms(
                            "用户指定算法：",
                            std::set<std::string>(options.algorithms.begin(), options.algorithms.end()));
                    std::cout << "Query candidates: count=" << result.algorithms.size() << " ret=" << result.ret
                              << " [";
                    for (size_t index = 0; index < result.algorithms.size(); ++index) {
                        if (index != 0)
                            std::cout << ", ";
                        std::cout << result.algorithms[index];
                    }
                    std::cout << "]\n";
                }
                return result;
            },
            coordinator);
        if (writer) {
            for (const auto& query : plan.queries)
                writer->WriteQuery(query);
        }
        if (!plan.Ok()) {
            if (writer)
                writer->WriteFailure(plan.failure_stage, 0, plan.error);
            std::cerr << "rank=" << context.global_rank << " query/plan failed: " << plan.error << '\n';
            finish_profiling();
            cleanup();
            return 1;
        }
        if (writer)
            PrintAlgorithmSelection(options, plan);
#ifdef MC2_BENCH_QUERY_ONLY
        if (writer)
            std::cout << "Query completed: " << plan.queries.size() << " request(s); no performance run.\n";
        cleanup();
        return 0;
#else
        size_t runnable = 0;
        size_t skipped = 0;
        for (const auto& entry : plan.plan) {
            if (writer)
                writer->WritePlan(entry);
            if (entry.kind == PlanKind::Unsupported) {
                ++skipped;
                if (writer)
                    std::cout << "Unsupported; skipped: "
                              << (entry.algorithm.empty() ? "no candidates" : entry.algorithm)
                              << " bytes=" << entry.requested_bytes << '\n';
            } else
                ++runnable;
        }
        if (writer)
            std::cout << "Test plan: executable_entries=" << runnable << " skipped_entries=" << skipped << '\n';
        if (runnable == 0) {
            if (writer)
                std::cout << "No executable candidates; measured_entries=0. No NPU warmup or measurement was run.\n";
            finish_profiling();
            cleanup();
            return 0;
        }

        if (backend.query_stub) {
            stage = "device_initialization";
            initialize();
        }
        const std::string directory = Environment("MC2_BENCH_RANK_DIR");
        const std::string sample_path = directory.empty() ?
                                            "native_samples_rank_" + std::to_string(context.global_rank) + ".json" :
                                            directory + "/native_samples.json";
        OpenNew(samples, sample_path);
        samples << "{\"schema_version\":1,\"query_source\":\"" << query_source << "\",\"measurement_mode\":\""
                << backend.measurement_mode
                << "\",\"execution_source\":\"native_acl_aclnn\","
                   "\"timing_source\":\"acl_event\",\"rank\":"
                << context.global_rank << ",\"backend_pid\":" << getpid() << ",\"world_size\":" << options.ranks
                << ",\"engine\":\"" << EngineName(options.engine) << "\",\"device_id\":" << device
                << ",\"warmup\":" << backend.warmup << ",\"iterations\":" << backend.iterations << ",\"samples\":[";
        device_session.reset(new NativeDeviceSession(
            options, context.global_rank, communication.communicator.Name(), communication.communicator.Get(), stream,
            backend.profiling));
        int exit_code = 0;
        size_t measured_entries = 0;
        for (const auto& entry : plan.plan) {
            if (entry.kind == PlanKind::Unsupported)
                continue;
            active = TestResult();
            active.entry = entry;
            active.batch = backend.profiling ? 1 : 0;
            active.shape = MakeCaseShape(options, entry.requested_bytes);
#ifdef MC2_BENCH_CCU_BACKEND
            if (options.op == Op::AllToAllVWrite)
                active.write_layout = MakeAllToAllVWriteLayout(options, entry.requested_bytes);
#endif
            active.status = ResultStatus::AlgorithmUnconfirmed;
            active_entry = true;
            stage = "execution";
            if (writer)
                std::cout << "Executing entry: id=" << entry.id << " bytes=" << entry.requested_bytes
                          << " algorithm=" << entry.algorithm << " warmup=" << backend.warmup
                          << " iterations=" << backend.iterations << '\n';
            CheckMpi(MPI_Barrier(MPI_COMM_WORLD), "MPI_Barrier(entry)");
            NativeDeviceResult measured;
            bool valid = true;
            std::string validation_error;
            try {
                measured = device_session->Run(entry, active.shape, backend.warmup, backend.iterations);
                valid = measured.validated;
            } catch (const NativeDeviceValidationError& error) {
                valid = false;
                validation_error = error.what();
            }
            const bool all_valid = coordinator.AllTrue(valid);
            if (!all_valid) {
                if (!valid)
                    std::cerr << "Validation failed rank=" << context.global_rank << ": "
                              << (validation_error.empty() ? "output mismatch" : validation_error) << '\n';
                active.status = ResultStatus::ValidationFailed;
                active.ret = 1;
                active.failure_stage = "validation";
                active.error = validation_error.empty() ? "output validation failed on a rank" : validation_error;
                if (writer)
                    writer->WriteResult(active);
                exit_code = 1;
                active_entry = false;
                break;
            }
            if (!std::isfinite(measured.event_us) || measured.event_us <= 0)
                throw std::runtime_error("ACL Event mean must be finite and positive");
            double root_us = measured.event_us;
            CheckMpi(MPI_Bcast(&root_us, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD), "MPI_Bcast(rank0 event)");
            WriteSample(samples, first_sample, entry, measured, active.batch);
            Metric kernel;
            kernel.name = "api_kernel_us";
            kernel.value = root_us;
            active.metrics.push_back(kernel);
#ifdef MC2_BENCH_NATIVE_PROFILING
            if (profiling) {
                stage = "profiling_collection";
                profiling->Record(active, measured, backend.warmup, backend.iterations);
            }
#endif
            if (writer) {
                writer->WriteResult(active);
                std::cout << "NPU validated bytes=" << entry.requested_bytes << " algorithm=" << entry.algorithm
                          << " acl_event_rank0_mean_us=" << std::setprecision(10) << root_us
                          << " validation=PASS actual_algorithm=unconfirmed\n";
            }
            ++measured_entries;
            active_entry = false;
        }
        samples << "]}\n";
        samples.flush();
        if (!samples)
            throw std::runtime_error("failed to finalize native samples");
        finish_profiling();
        cleanup();
        if (writer)
            std::cout << "Benchmark finished: measured_entries=" << measured_entries << " skipped_entries=" << skipped
                      << " status=" << (exit_code == 0 ? "PASS" : "FAIL") << '\n';
        return exit_code;
#endif
    } catch (const std::exception& error) {
        if (writer) {
            try {
                if (active_entry) {
                    active.status = ResultStatus::ExecutionFailed;
                    active.ret = 1;
                    active.failure_stage = stage;
                    active.error = error.what();
                    active.metrics.clear();
                    writer->WriteResult(active);
                }
                writer->WriteFailure(stage, 1, error.what());
            } catch (...) {
            }
        }
        std::cerr << "NPU backend failed rank=" << context.global_rank << " stage=" << stage << ": " << error.what()
                  << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        std::_Exit(1);
    }
}
} // namespace
} // namespace asccomm_api_bench

int main(int argc, char** argv)
{
    using namespace asccomm_api_bench;
    // The launcher retains these streams in files and may also forward them
    // through MPI pipes. Flush stage boundaries before entering device calls.
    std::cout << std::unitbuf;
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS)
        return 1;
    try {
        CheckMpi(MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN), "MPI_Comm_set_errhandler");
        MpiCoordinator coordinator(MPI_COMM_WORLD);
        if (argc == 2 && std::string(argv[1]) == "--help") {
            if (coordinator.Rank() == 0) {
#ifdef MC2_BENCH_QUERY_ONLY
                std::cout << "asccomm_api_bench_query\n"
                          << Usage()
                          << "Query only: requires the SDK/communicator for native queries; no operator execution, "
                             "warmup or profiling.\n";
#else
                std::cout << "asccomm_api_bench_backend\n"
                          << Usage()
#ifdef MC2_BENCH_CCU_BACKEND
                          << "Device backend: CCU_SCHED, six operators; requires a real SDK/NPU.\n"
#else
                          << "Device backend: AICPU_TS, five operators; requires a real SDK/NPU.\n"
#endif
                             "Queries use the formal MC2 API by default.\n"
                             "No simulated performance or algorithm-hit evidence.\n";
#endif
            }
            return MPI_Finalize() == MPI_SUCCESS ? 0 : 1;
        }
        AbortTransportCoordinator transport(coordinator);
        const int result = RunBackend(ParseBackend(argc, argv), transport);
        return MPI_Finalize() == MPI_SUCCESS ? result : 1;
    } catch (const std::exception& error) {
        std::cerr << "backend failed: " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
