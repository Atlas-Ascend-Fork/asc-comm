/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "native_device.h"

#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/time.h>
#include "case_shape.h"
#include "options.h"
#include "legacy/hccl_api_test_common.h"
#include "legacy/operators/hccl_allreduce_test.h"
#include "legacy/operators/hccl_allgather_test.h"
#include "legacy/operators/hccl_reducescatter_test.h"
#include "legacy/operators/hccl_alltoall_test.h"
#include "legacy/operators/hccl_alltoallv_test.h"
#ifdef MC2_BENCH_CCU_BACKEND
#include "native_write_adapter.h"
#endif

namespace asccomm_api_bench {
namespace {
namespace old = hccl_api_test;

std::string AlgorithmLogTimestamp()
{
    timeval now{};
    tm local{};
    char date[32]{};
    if (gettimeofday(&now, nullptr) != 0 || localtime_r(&now.tv_sec, &local) == nullptr ||
        strftime(date, sizeof(date), "%Y-%m-%d-%H:%M:%S", &local) == 0)
        return {};
    std::ostringstream result;
    result << date << '.' << std::setfill('0') << std::setw(3) << now.tv_usec / 1000 << '.' << std::setw(3)
           << now.tv_usec % 1000;
    return result.str();
}

void Check(int64_t ret, const char* operation)
{
    if (ret != ACL_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed, ret=" + std::to_string(ret));
}

std::unique_ptr<old::HcclOpBaseTest> MakeOperator(Op op)
{
    switch (op) {
        case Op::AllReduce:
            return std::unique_ptr<old::HcclOpBaseTest>(new old::HcclOpBaseAllreduceTest);
        case Op::AllGather:
            return std::unique_ptr<old::HcclOpBaseTest>(new old::HcclOpBaseAllgatherTest);
        case Op::ReduceScatter:
            return std::unique_ptr<old::HcclOpBaseTest>(new old::HcclOpBaseReducescatterTest);
        case Op::AllToAll:
            return std::unique_ptr<old::HcclOpBaseTest>(new old::HcclOpBaseAlltoallTest);
        case Op::AllToAllV:
            return std::unique_ptr<old::HcclOpBaseTest>(new old::HcclOpBaseAlltoallvTest);
        case Op::AllToAllVWrite:
            break;
    }
    throw std::invalid_argument("operator requires the CCU communication-window adapter");
}

bool SameShape(const CaseShape& a, const CaseShape& b)
{
    return a.requested_bytes == b.requested_bytes && a.input_count == b.input_count &&
           a.output_count == b.output_count && a.input_bytes == b.input_bytes && a.output_bytes == b.output_bytes &&
           a.bandwidth_bytes == b.bandwidth_bytes && a.per_rank_count == b.per_rank_count &&
           a.per_peer_count == b.per_peer_count;
}
} // namespace

void ValidateNativeDeviceOptions(const Options& options)
{
    ValidateOptions(options);
#ifdef MC2_BENCH_CCU_BACKEND
    if (options.engine != Engine::CcuMs && options.engine != Engine::CcuSched)
        throw std::invalid_argument("native CCU backend requires the CCU scheduling engine");
#else
    if (options.engine != Engine::AicpuTs || options.op == Op::AllToAllVWrite)
        throw std::invalid_argument("native AICPU backend requires an AICPU tensor operator");
#endif
    ValidateMeasurementDType(options);
    if ((options.op == Op::AllToAllV || options.op == Op::AllToAllVWrite) && options.ranks > 384)
        throw std::invalid_argument("AlltoAllV and AlltoAllVWrite custom operators support at most 384 ranks");
}

struct NativeDeviceSession::Impl {
    Options options;
    old::RuntimeContext context;
    bool collect_profile;
    bool failed = false;

    Impl(const Options& selected, uint32_t rank, const std::string& name, void* comm, aclrtStream stream, bool profile)
        : options(selected), collect_profile(profile)
    {
        ValidateNativeDeviceOptions(options);
#ifndef MC2_BENCH_NATIVE_PROFILING
        if (profile)
            throw std::invalid_argument("native profiling was disabled at build time");
#endif
        if (rank >= options.ranks || options.ranks > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            options.devices_per_node > static_cast<uint32_t>(std::numeric_limits<int>::max()) || name.empty() ||
            name.size() >= sizeof(context.comm_name) || !comm || !stream)
            throw std::invalid_argument("invalid legacy execution session context");
        context.rank = static_cast<int>(rank);
        context.rank_size = static_cast<int>(options.ranks);
        context.stream = stream;
        context.comm = static_cast<HcclComm>(comm);
        std::memcpy(context.comm_name, name.c_str(), name.size() + 1);
        try {
            Check(aclrtSetStreamFailureMode(stream, 1), "aclrtSetStreamFailureMode");
            Check(aclrtCreateStream(&context.control_stream), "aclrtCreateStream(control)");
            Check(aclrtSetStreamFailureMode(context.control_stream, 1), "aclrtSetStreamFailureMode(control)");
            Check(aclrtCreateNotify(&context.gate_notify, ACL_NOTIFY_DEFAULT), "aclrtCreateNotify");
            Check(aclrtCreateEvent(&context.start_event), "aclrtCreateEvent(start)");
            Check(aclrtCreateEvent(&context.end_event), "aclrtCreateEvent(end)");
        } catch (...) {
            Release();
            throw;
        }
    }

    void Release()
    {
        if (failed)
            return; // The backend aborts MPI; live communication resources must stay valid.
        if (context.end_event)
            aclrtDestroyEvent(context.end_event);
        if (context.start_event)
            aclrtDestroyEvent(context.start_event);
        if (context.gate_notify)
            aclrtDestroyNotify(context.gate_notify);
        if (context.control_stream)
            aclrtDestroyStream(context.control_stream);
    }
    ~Impl() { Release(); }

    void ClearObservers()
    {
        context.submitted = nullptr;
        context.completed = nullptr;
        context.timed = nullptr;
        context.read_wait = nullptr;
        context.shape_ready = nullptr;
        context.fatal = nullptr;
    }

    NativeDeviceResult Run(const PlanEntry& entry, const CaseShape& shape, uint32_t warmup, uint32_t iterations)
    {
        if (failed || !iterations || iterations > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            warmup > static_cast<uint32_t>(std::numeric_limits<int>::max()) || entry.kind != PlanKind::Supported ||
            !IsExternalAlgorithmName(entry.algorithm) ||
            !SameShape(shape, MakeCaseShape(options, entry.requested_bytes)))
            throw std::invalid_argument("invalid legacy execution entry");
        NativeDeviceResult result;
        result.algorithm_log_begin = AlgorithmLogTimestamp();
        std::unique_ptr<old::HcclOpBaseTest> executor;
#ifdef MC2_BENCH_CCU_BACKEND
        if (options.op == Op::AllToAllVWrite)
            executor =
                MakeLegacyWriteAdapter(options, entry, static_cast<uint32_t>(context.rank), context.comm, result);
        else
#endif
            executor = MakeOperator(options.op);
        executor->need_ranksize_alignment = options.op != Op::AllReduce;
        old::Options old_options;
        old_options.min_bytes = entry.requested_bytes;
        old_options.max_bytes = entry.requested_bytes;
        old_options.iters = static_cast<int>(iterations);
        old_options.warmup_iters = static_cast<int>(warmup);
        old_options.npus = static_cast<int>(options.devices_per_node);
        old_options.datatype = options.dtype;
        old_options.reduce_op = options.reduce;
        old_options.accelerator = EngineName(options.engine);
        old_options.accelerator_mode = CommEngineCode(options.engine);
        old_options.algorithm = entry.algorithm;
        old::AlignOptionsLikeHccl(old_options, *executor, context.rank_size);
        executor->set_test_context(context, old_options, *old::FindDType(options.dtype));

        double total_us = 0;
        uint32_t timed_count = 0;
        NativeIterationSample current;
        uint64_t wait_ns = 0;
        context.fatal = [&](const char* stage, int code) {
            failed = true;
            throw std::runtime_error(std::string("legacy execution ") + stage + " failed, ret=" + std::to_string(code));
        };
        context.shape_ready = [&](const old::CaseShape& actual) {
            return actual.input_count == shape.input_count && actual.output_count == shape.output_count &&
                           actual.input_bytes == shape.input_bytes && actual.output_bytes == shape.output_bytes ?
                       0 :
                       -1;
        };
        // Observe the operator submission before the trailing Event changes last-task identity.
        // Return failures to the legacy collective failure path so no peer is left at the gate.
        context.submitted = [&](bool measured, int iteration) {
            current = NativeIterationSample();
            current.measured = measured;
            current.iteration = static_cast<uint32_t>(iteration) + (measured ? 1U : 0U);
#ifdef MC2_BENCH_NATIVE_PROFILING
            if (collect_profile) {
                int32_t stream_id = -1;
                int ret = aclrtGetThreadLastTaskId(&current.task_id);
                if (ret != ACL_SUCCESS)
                    return ret;
                ret = aclrtStreamGetId(context.stream, &stream_id);
                if (ret != ACL_SUCCESS || stream_id < 0)
                    return ret != ACL_SUCCESS ? ret : -1;
                current.stream_id = static_cast<uint32_t>(stream_id);
            }
#endif
            if (collect_profile && !measured)
                result.iteration_samples.push_back(current);
            return 0;
        };
        context.completed = [&](int, const old::CaseResources& resources) {
#ifdef MC2_BENCH_CCU_BACKEND
            const bool write = options.op == Op::AllToAllVWrite;
            const void* report_address = resources.input.device_addr;
            uint64_t report[2] = {};
            const int ret =
                aclrtMemcpy(report, sizeof(report), report_address, sizeof(report), ACL_MEMCPY_DEVICE_TO_HOST);
            if (ret != ACL_SUCCESS)
                return ret;
            if (report[0] != 0xA5A55A5AA5A55A5AULL || report[1] == 0)
                return -1;
            // Read the same magic + duration-in-nanoseconds report as the legacy runner.
            wait_ns = report[1];
            current.wait_report_present = true;
            current.wait_duration_ns = wait_ns;
            if (write) {
                result.write_report_present = true;
                result.wait_duration_ns = wait_ns;
            }
#else
            (void)resources;
#endif
            return 0;
        };
        context.read_wait = [&](const old::CaseResources&, uint64_t& value) {
            value = wait_ns;
            return value > 0 ? 0 : -1;
        };
        context.timed = [&](int, double local_us) {
            total_us += local_us;
            ++timed_count;
            current.event_us = local_us;
            if (collect_profile)
                result.iteration_samples.push_back(current);
        };
        int ret = 0;
        try {
            // This is the imported runner, including resource setup, warmup,
            // per-iteration MPI/Notify alignment, Events, resets and final validation.
            ret = executor->opbase_test_by_data_size();
        } catch (...) {
            ClearObservers();
            throw;
        }
        ClearObservers();
        result.algorithm_log_end = AlgorithmLogTimestamp();
        if (executor->validation_failed())
            throw NativeDeviceValidationError("legacy executor output validation failed");
        if (ret != 0)
            throw std::runtime_error("legacy executor failed, ret=" + std::to_string(ret));
        if (timed_count != iterations || !std::isfinite(total_us) || total_us <= 0)
            throw std::runtime_error("legacy executor did not produce all measured Event intervals");
        result.event_us = total_us / iterations;
        result.validated = true;
        if (options.op == Op::AllToAllVWrite)
            result.window_verified_bytes = shape.output_bytes;
        return result;
    }
};

NativeDeviceSession::NativeDeviceSession(
    const Options& options, uint32_t rank, const std::string& comm_name, void* communicator, aclrtStream stream,
    bool collect_profile)
    : impl_(new Impl(options, rank, comm_name, communicator, stream, collect_profile))
{}
NativeDeviceSession::~NativeDeviceSession() = default;
NativeDeviceResult NativeDeviceSession::Run(
    const PlanEntry& entry, const CaseShape& shape, uint32_t warmup, uint32_t iterations)
{
    return impl_->Run(entry, shape, warmup, iterations);
}
} // namespace asccomm_api_bench
