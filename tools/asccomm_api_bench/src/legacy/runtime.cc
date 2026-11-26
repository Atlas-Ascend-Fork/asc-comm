/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Reused from hccl_api_test 138ac18b6c496ca3faebab0fa8e8038757ec19a7
// Original source: src/hccl_api_test.cc

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "mpi.h"
#include "hccl_api_test_common.h"

namespace hccl_api_test {

constexpr uint64_t KILO = 1024ULL;
constexpr uint64_t MEGA = KILO * KILO;
constexpr uint64_t HCCL_DATA_ALIGN_SIZE = 512;
constexpr uint64_t HCCL_DATA_ALIGN_THRESHOLD = 16ULL * MEGA;
constexpr uint64_t HCCL_ALLTOALL_GRANULARITY = 128;
constexpr size_t MAX_VERBOSE_MISMATCHES = 80;
constexpr int BEST_EFFORT_RELEASE_TIMEOUT_MS = 1000;
constexpr uint64_t WAIT_REPORT_MAGIC = 0xA5A55A5AA5A55A5AULL;
constexpr size_t WAIT_REPORT_BYTES = 2 * sizeof(uint64_t);

void DeviceTensor::Reset()
{
    if (tensor != nullptr) {
        aclDestroyTensor(tensor);
        tensor = nullptr;
    }
    if (device_addr != nullptr) {
        aclrtFree(device_addr);
        device_addr = nullptr;
    }
    bytes = 0;
}

void CaseResources::ResetExecutor()
{
    if (executor != nullptr) {
        // A non-null executor has not been handed to the stage-two API.
        aclDestroyAclOpExecutor(executor);
        executor = nullptr;
    }
}

void CaseResources::Reset()
{
    ResetExecutor();
    input.Reset();
    output.Reset();
    input_splits.Reset();
    output_splits.Reset();
    if (workspace != nullptr) {
        aclrtFree(workspace);
        workspace = nullptr;
    }
    workspace_size = 0;
    workspace_capacity = 0;
}

const std::vector<DTypeInfo>& SupportedDTypes()
{
    static const std::vector<DTypeInfo> types = {
        {"int8", ACL_INT8, 1, false},
        {"int16", ACL_INT16, 2, false},
        {"int32", ACL_INT32, 4, false},
        {"int64", ACL_INT64, 8, false},
        {"uint8", ACL_UINT8, 1, false},
        {"uint16", ACL_UINT16, 2, false},
        {"uint32", ACL_UINT32, 4, false},
        {"uint64", ACL_UINT64, 8, false},
        {"fp16", ACL_FLOAT16, 2, false},
        {"fp32", ACL_FLOAT, 4, false},
        {"fp64", ACL_DOUBLE, 8, false},
        {"bfp16", ACL_BF16, 2, false},
        {"bf16", ACL_BF16, 2, false},
        {"hif8", ACL_HIFLOAT8, 1, true},
        {"fp8e4m3", ACL_FLOAT8_E4M3FN, 1, true},
        {"fp8e5m2", ACL_FLOAT8_E5M2, 1, true},
        {"fp8e8m0", ACL_FLOAT8_E8M0, 1, true},
    };
    return types;
}

const DTypeInfo* FindDType(const std::string& name)
{
    const std::vector<DTypeInfo>& types = SupportedDTypes();
    for (size_t i = 0; i < types.size(); ++i) {
        if (name == types[i].name) {
            return &types[i];
        }
    }
    return nullptr;
}

void AlignOptionsLikeHccl(Options& options, const HcclTest& opbase, int rank_size)
{
    if (!opbase.need_ranksize_alignment)
        return;
    const uint64_t ranks = static_cast<uint64_t>(rank_size);
    const uint64_t large_alignment = ranks * HCCL_DATA_ALIGN_SIZE;
    const auto align_bytes = [ranks, large_alignment](uint64_t bytes) {
        bytes = std::max(bytes, ranks);
        const uint64_t alignment = bytes > HCCL_DATA_ALIGN_THRESHOLD ? large_alignment : ranks;
        return bytes / alignment * alignment;
    };
    options.min_bytes = align_bytes(options.min_bytes);
    options.max_bytes = align_bytes(options.max_bytes);
    if (options.step_bytes != 0) {
        options.step_bytes = (options.step_bytes + ranks - 1) / ranks * ranks;
    }
}

int ReduceOpValue(const std::string& name)
{
    if (name == "sum")
        return static_cast<int>(HCCL_REDUCE_SUM);
    if (name == "prod")
        return static_cast<int>(HCCL_REDUCE_PROD);
    if (name == "max")
        return static_cast<int>(HCCL_REDUCE_MAX);
    return static_cast<int>(HCCL_REDUCE_MIN);
}

uint16_t FloatToHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000U;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffU) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffU;
    if (exponent <= 0) {
        if (exponent < -10)
            return static_cast<uint16_t>(sign);
        mantissa = (mantissa | 0x800000U) >> (1 - exponent);
        return static_cast<uint16_t>(sign | ((mantissa + 0x1000U) >> 13));
    }
    if (exponent >= 31)
        return static_cast<uint16_t>(sign | 0x7c00U);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | ((mantissa + 0x1000U) >> 13));
}

uint16_t FloatToBfloat16(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    bits += 0x7fffU + ((bits >> 16) & 1U);
    return static_cast<uint16_t>(bits >> 16);
}

template <typename T>
std::vector<uint8_t> ScalarBuffer(T value)
{
    std::vector<uint8_t> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}

std::vector<uint8_t> EncodeScalar(const DTypeInfo& dtype, int64_t value)
{
    const std::string name(dtype.name);
    if (dtype.zero_only_init)
        return std::vector<uint8_t>(dtype.size, 0);
    if (name == "int8")
        return ScalarBuffer<int8_t>(static_cast<int8_t>(value));
    if (name == "int16")
        return ScalarBuffer<int16_t>(static_cast<int16_t>(value));
    if (name == "int32")
        return ScalarBuffer<int32_t>(static_cast<int32_t>(value));
    if (name == "int64")
        return ScalarBuffer<int64_t>(value);
    if (name == "uint8")
        return ScalarBuffer<uint8_t>(static_cast<uint8_t>(value));
    if (name == "uint16")
        return ScalarBuffer<uint16_t>(static_cast<uint16_t>(value));
    if (name == "uint32")
        return ScalarBuffer<uint32_t>(static_cast<uint32_t>(value));
    if (name == "uint64")
        return ScalarBuffer<uint64_t>(static_cast<uint64_t>(value));
    if (name == "fp16")
        return ScalarBuffer<uint16_t>(FloatToHalf(static_cast<float>(value)));
    if (name == "bfp16" || name == "bf16") {
        return ScalarBuffer<uint16_t>(FloatToBfloat16(static_cast<float>(value)));
    }
    if (name == "fp32")
        return ScalarBuffer<float>(static_cast<float>(value));
    if (name == "fp64")
        return ScalarBuffer<double>(static_cast<double>(value));
    return std::vector<uint8_t>(dtype.size, 0);
}

void FillRepeated(
    std::vector<uint8_t>& buffer, size_t byte_offset, uint64_t element_count, const std::vector<uint8_t>& scalar)
{
    if (element_count == 0 || scalar.empty())
        return;
    const uint64_t total_bytes_u64 = element_count * static_cast<uint64_t>(scalar.size());
    const size_t total_bytes = static_cast<size_t>(total_bytes_u64);
    uint8_t* destination = buffer.data() + byte_offset;
    std::memcpy(destination, scalar.data(), scalar.size());
    size_t filled = scalar.size();
    while (filled < total_bytes) {
        const size_t copy_bytes = std::min(filled, total_bytes - filled);
        std::memcpy(destination + filled, destination, copy_bytes);
        filled += copy_bytes;
    }
}

bool CheckedMultiply(uint64_t lhs, uint64_t rhs, uint64_t& result)
{
    if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs)
        return false;
    result = lhs * rhs;
    return true;
}

bool BytesToElementCount(uint64_t requested_bytes, const DTypeInfo& dtype, CaseShape& shape, uint64_t& count)
{
    shape = CaseShape();
    shape.requested_bytes = requested_bytes;
    count = requested_bytes / dtype.size + (requested_bytes % dtype.size == 0 ? 0 : 1);
    count = std::max<uint64_t>(1, count);
    return count <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool AlignElementCount(uint64_t& count, uint64_t alignment)
{
    if (alignment == 0 || count > std::numeric_limits<uint64_t>::max() - (alignment - 1))
        return false;
    count = (count + alignment - 1) / alignment * alignment;
    return count <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool AlignAllToAllElementCount(uint64_t& count, int rank_size)
{
    const uint64_t ranks = static_cast<uint64_t>(rank_size);
    if (ranks > std::numeric_limits<uint64_t>::max() / HCCL_ALLTOALL_GRANULARITY)
        return false;
    const uint64_t large_alignment = ranks * HCCL_ALLTOALL_GRANULARITY;
    return AlignElementCount(count, count >= large_alignment ? large_alignment : ranks);
}

bool FinalizeCaseShape(const DTypeInfo& dtype, bool data_size_uses_output, CaseShape& shape)
{
    if (!CheckedMultiply(shape.input_count, dtype.size, shape.input_bytes))
        return false;
    if (!CheckedMultiply(shape.output_count, dtype.size, shape.output_bytes))
        return false;
    shape.data_bytes = data_size_uses_output ? shape.output_bytes : shape.input_bytes;
    if (shape.input_bytes > std::numeric_limits<size_t>::max() ||
        shape.output_bytes > std::numeric_limits<size_t>::max())
        return false;
    return true;
}

int CreateDeviceTensor(
    const std::vector<int64_t>& shape, aclDataType dtype, size_t element_size, const void* host_data,
    DeviceTensor& result, aclrtMemMallocPolicy policy, size_t min_allocation_bytes)
{
    uint64_t count = 1;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] <= 0 || !CheckedMultiply(count, static_cast<uint64_t>(shape[i]), count))
            return -1;
    }
    uint64_t byte_count = 0;
    if (!CheckedMultiply(count, element_size, byte_count) || byte_count > std::numeric_limits<size_t>::max())
        return -1;
    result.bytes = static_cast<size_t>(byte_count);
    const size_t allocation_bytes =
        static_cast<size_t>(byte_count) < min_allocation_bytes ? min_allocation_bytes : static_cast<size_t>(byte_count);
    aclError ret = aclrtMalloc(&result.device_addr, allocation_bytes, policy);
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "aclrtMalloc failed, ret=%d, bytes=%zu\n", ret, allocation_bytes);
        return ret;
    }
    if (host_data != nullptr) {
        ret = aclrtMemcpy(result.device_addr, result.bytes, host_data, result.bytes, ACL_MEMCPY_HOST_TO_DEVICE);
        if (ret != ACL_SUCCESS) {
            std::fprintf(stderr, "Host-to-device copy failed, ret=%d\n", ret);
            result.Reset();
            return ret;
        }
    }

    std::vector<int64_t> strides(shape.size(), 1);
    for (int64_t index = static_cast<int64_t>(shape.size()) - 2; index >= 0; --index) {
        strides[static_cast<size_t>(index)] =
            shape[static_cast<size_t>(index + 1)] * strides[static_cast<size_t>(index + 1)];
    }
    const int64_t storage_shape[] = {static_cast<int64_t>(count)};
    result.tensor = aclCreateTensor(
        shape.data(), shape.size(), dtype, strides.data(), 0, ACL_FORMAT_ND, storage_shape, 1, result.device_addr);
    if (result.tensor == nullptr) {
        std::fprintf(stderr, "aclCreateTensor failed.\n");
        result.Reset();
        return -1;
    }
    return 0;
}

HcclTest::HcclTest() : data(&data_storage_) {}

void HcclTest::set_test_context(RuntimeContext& runtime_context, const Options& test_options, const DTypeInfo& dtype)
{
    context = &runtime_context;
    options = &test_options;
    dtype_info = &dtype;
    data->min_bytes = test_options.min_bytes;
    data->max_bytes = test_options.max_bytes;
    data->step_bytes = test_options.step_bytes;
    data->step_factor = test_options.step_factor_set ? test_options.step_factor : 1.0;
}

void HcclTest::init_data_count()
{
    uint64_t count = 0;
    if (!BytesToElementCount(data->data_size, *dtype_info, case_shape, count)) {
        data->count = 0;
        return;
    }
    data->count = count;
    data->type_size = dtype_info->size;
}

int HcclTest::get_buff_size()
{
    init_data_count();
    if (data->count == 0 || data->type_size == 0)
        return -1;

    malloc_kSize = init_malloc_Ksize_by_data();
    if (malloc_kSize == 0)
        return -1;

    size_t send_bytes = 0;
    size_t recv_bytes = 0;
    init_send_recv_size_by_data(send_bytes, recv_bytes);
    if (!init_case_shape())
        return -1;
    if (send_bytes != case_shape.input_bytes || recv_bytes != case_shape.output_bytes) {
        std::fprintf(
            stderr, "%s buffer size mismatch: send=%zu/%llu, recv=%zu/%llu.\n", op_name(), send_bytes,
            static_cast<unsigned long long>(case_shape.input_bytes), recv_bytes,
            static_cast<unsigned long long>(case_shape.output_bytes));
        return -1;
    }
    return 0;
}

int HcclOpBaseTest::init_buf_val(CaseResources& res)
{
    const int64_t input_value = is_reduction() ? 1 : (context->rank % 31) + 1;
    std::vector<uint8_t> input(static_cast<size_t>(case_shape.input_bytes));
    FillRepeated(input, 0, case_shape.input_count, EncodeScalar(*dtype_info, input_value));
    if (&res == &resources) {
        input_seed = input; // 供 reset_input() 恢复被测 case 初值（对齐轮不参与）
    }
    // input 头部承载内核写回的 Wait 耗时报告，极端小尺寸时补足分配防止越界写。
    int ret = CreateDeviceTensor(
        case_shape.input_shape, dtype_info->acl_type, dtype_info->size, input.data(), res.input,
        ACL_MEM_MALLOC_HUGE_ONLY, WAIT_REPORT_BYTES);
    if (ret != 0)
        return ret;
    return CreateDeviceTensor(
        case_shape.output_shape, dtype_info->acl_type, dtype_info->size, nullptr, res.output, ACL_MEM_MALLOC_HUGE_ONLY);
}

int HcclOpBaseTest::reset_input()
{
    if (resources.input.device_addr == nullptr || input_seed.empty())
        return 0;
    const aclError ret = aclrtMemcpy(
        resources.input.device_addr, resources.input.bytes, input_seed.data(), input_seed.size(),
        ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "%s reset_input H2D copy failed, rank=%d, ret=%d\n", op_name(), context->rank, ret);
        return static_cast<int>(ret);
    }
    return 0;
}

int EnsureWorkspace(RuntimeContext& context, uint64_t required, CaseResources& resources)
{
    if (required <= resources.workspace_capacity)
        return 0;
    if (resources.workspace != nullptr) {
        aclError sync_ret = aclrtSynchronizeStream(context.stream);
        if (sync_ret != ACL_SUCCESS)
            return sync_ret;
        aclrtFree(resources.workspace);
        resources.workspace = nullptr;
        resources.workspace_capacity = 0;
    }
    if (required == 0)
        return 0;
    aclError ret = aclrtMalloc(&resources.workspace, required, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        std::fprintf(
            stderr, "Workspace allocation failed, ret=%d, bytes=%llu\n", ret,
            static_cast<unsigned long long>(required));
        return ret;
    }
    resources.workspace_capacity = required;
    return 0;
}

int PrepareSingleUseExecutor(
    RuntimeContext& context, uint64_t workspace_size, aclOpExecutor* executor, CaseResources& resources)
{
    if (executor == nullptr) {
        std::fprintf(stderr, "GetWorkspaceSize returned a null executor.\n");
        return -1;
    }
    if (resources.executor != nullptr) {
        std::fprintf(stderr, "GetWorkspaceSize returned an executor while the previous executor is pending.\n");
        aclDestroyAclOpExecutor(executor);
        return -1;
    }
    resources.executor = executor;
    // Stage two consumes this executor. CaseResources only owns it until then.
    resources.workspace_size = workspace_size;
    return EnsureWorkspace(context, resources.workspace_size, resources);
}

std::vector<uint8_t> BuildSourceOrderedExpected(
    const RuntimeContext& context, const DTypeInfo& dtype, const CaseShape& shape, uint64_t block_count)
{
    std::vector<uint8_t> expected(static_cast<size_t>(shape.output_bytes));
    const size_t block_bytes = static_cast<size_t>(block_count * dtype.size);
    for (int source = 0; source < context.rank_size; ++source) {
        const size_t offset = static_cast<size_t>(source) * block_bytes;
        FillRepeated(expected, offset, block_count, EncodeScalar(dtype, (source % 31) + 1));
    }
    return expected;
}

std::vector<uint8_t> BuildReductionExpected(
    const RuntimeContext& context, const Options& options, const DTypeInfo& dtype, const CaseShape& shape)
{
    std::vector<uint8_t> expected(static_cast<size_t>(shape.output_bytes));
    int64_t result_value = 1;
    if (dtype.zero_only_init) {
        result_value = 0;
    } else if (options.reduce_op == "sum") {
        result_value = context.rank_size;
    }
    FillRepeated(expected, 0, shape.output_count, EncodeScalar(dtype, result_value));
    return expected;
}

int HcclOpBaseTest::check_buf_result()
{
    if (options->check == 0)
        return 0;
    std::vector<uint8_t> actual(static_cast<size_t>(case_shape.output_bytes));
    aclError ret = aclrtMemcpy(
        actual.data(), actual.size(), resources.output.device_addr, resources.output.bytes, ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "Device-to-host output copy failed, rank=%d, ret=%d\n", context->rank, ret);
        return 1;
    }
    const std::vector<uint8_t> expected = build_expected();
    size_t mismatches = 0;
    // 临时诊断：-c 2 时输出头部 64B hexdump，看 kernel 侧写的诊断快照（定位完删除）
    if (options->check == 2) {
        const size_t headBytes = actual.size() < 64U ? actual.size() : 64U;
        std::fprintf(stderr, "rank=%d head64:", context->rank);
        for (size_t index = 0; index < headBytes; ++index) {
            std::fprintf(stderr, " %02x", actual[index]);
        }
        std::fprintf(stderr, "\n");
    }
    for (size_t index = 0; index < actual.size(); ++index) {
        if (actual[index] == expected[index])
            continue;
        if (options->check == 2 && mismatches < MAX_VERBOSE_MISMATCHES) {
            std::fprintf(
                stderr, "rank=%d mismatch byte=%zu actual=0x%02x expected=0x%02x\n", context->rank, index,
                static_cast<unsigned>(actual[index]), static_cast<unsigned>(expected[index]));
        }
        ++mismatches;
    }
    if (mismatches != 0) {
        std::fprintf(
            stderr, "rank=%d verification failed: %zu/%zu bytes mismatch.\n", context->rank, mismatches, actual.size());
        return 1;
    }
    return 0;
}

namespace {

int ReadKernelWaitTimeNs(const void* device_addr, uint64_t& wait_ns)
{
    uint64_t report[2] = {0, 0};
    const aclError ret = aclrtMemcpy(report, sizeof(report), device_addr, sizeof(report), ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        return static_cast<int>(ret);
    }
    if (report[0] != WAIT_REPORT_MAGIC) {
        return -1;
    }
    wait_ns = report[1];
    return 0;
}

int SynchronizeDataSizeStatus(const RuntimeContext& context, int local_status, uint64_t data_size)
{
    const int local_failed = local_status == 0 ? 0 : 1;
    int any_failed = 0;
    const int mpi_ret = MPI_Allreduce(&local_failed, &any_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    if (mpi_ret != MPI_SUCCESS) {
        std::fprintf(
            stderr, "Data-size status MPI_Allreduce failed, rank=%d, data_size=%llu, ret=%d.\n", context.rank,
            static_cast<unsigned long long>(data_size), mpi_ret);
        return mpi_ret;
    }
    if (any_failed == 0)
        return 0;

    if (local_failed != 0) {
        std::fprintf(
            stderr, "Data-size test failed, rank=%d, data_size=%llu, local_status=%d.\n", context.rank,
            static_cast<unsigned long long>(data_size), local_status);
        return local_status;
    }
    if (context.rank == 0) {
        std::fprintf(
            stderr, "Data-size test failed on another rank, data_size=%llu.\n",
            static_cast<unsigned long long>(data_size));
    }
    return -1;
}

} // namespace

[[noreturn]] void AbortDeviceTiming(const RuntimeContext& context, bool gate_armed, const char* stage, int local_error)
{
    if (context.fatal)
        context.fatal(stage, local_error);
    std::fprintf(
        stderr, "Device timing failed, rank=%d, stage=%s, local_error=%d; aborting MPI job.\n", context.rank, stage,
        local_error);
    if (gate_armed && context.gate_notify != nullptr && context.control_stream != nullptr) {
        (void)aclrtRecordNotify(context.gate_notify, context.control_stream);
        (void)aclrtSynchronizeStreamWithTimeout(context.control_stream, BEST_EFFORT_RELEASE_TIMEOUT_MS);
    }
    std::fflush(stdout);
    std::fflush(stderr);
    (void)MPI_Abort(MPI_COMM_WORLD, 1);
    std::_Exit(1);
}

uint32_t NotifyTimeoutSeconds(int timeout_ms)
{
    constexpr uint64_t MILLISECONDS_PER_SECOND = 1000;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(timeout_ms) + MILLISECONDS_PER_SECOND - 1) / MILLISECONDS_PER_SECOND);
}

void AllreduceDeviceFailure(const RuntimeContext& context, bool gate_armed, const char* stage, int local_error)
{
    const int local_failure = local_error == 0 ? 0 : 1;
    int global_failure = 0;
    const int mpi_ret = MPI_Allreduce(&local_failure, &global_failure, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    if (mpi_ret != MPI_SUCCESS) {
        AbortDeviceTiming(context, gate_armed, "MPI_Allreduce", mpi_ret);
    }
    if (global_failure != 0) {
        AbortDeviceTiming(context, gate_armed, stage, local_error);
    }
}

int HcclOpBaseTest::prepare_iteration_executor(const char* phase, int iteration)
{
    const int ret = init_op_executor(resources);
    const int local_failed = ret == 0 ? 0 : 1;
    int any_failed = 0;
    const int mpi_ret = MPI_Allreduce(&local_failed, &any_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    if (mpi_ret != MPI_SUCCESS) {
        AbortDeviceTiming(*context, false, "executor preparation MPI_Allreduce", mpi_ret);
    }
    if (any_failed == 0)
        return 0;

    resources.ResetExecutor();
    if (ret != 0) {
        std::fprintf(
            stderr, "%s %s executor preparation failed, rank=%d, iteration=%d, ret=%d\n", op_name(), phase,
            context->rank, iteration, ret);
        return ret;
    }
    if (context->rank == 0) {
        std::fprintf(
            stderr, "%s %s executor preparation failed on another rank, iteration=%d.\n", op_name(), phase, iteration);
    }
    return -1;
}

namespace {
// 对齐轮数据尺寸下限：实际形态由各算子自身最小 case 逻辑决定
// （如 RS 会把 count 对齐到 rank 倍数），此处只要求"远小于被测尺寸"。
constexpr uint64_t kAlignDataBytes = 16;
} // namespace

HcclOpBaseTest::~HcclOpBaseTest() { cleanup_align_op(); }

int HcclOpBaseTest::setup_align_op()
{
    if (align_ready)
        return 0;

    // 同算子 tiny case：临时把数据尺寸切到 tiny，走本算子自身的
    // get_buff_size / init_buf_val / init_op_executor 构建路径。
    const uint64_t saved_data_size = data->data_size;
    const uint64_t saved_count = data->count;
    const CaseShape saved_shape = case_shape;
    const size_t saved_malloc_kSize = malloc_kSize;

    data->data_size = kAlignDataBytes;
    int ret = get_buff_size();
    if (ret == 0)
        ret = init_buf_val(align_resources);
    if (ret == 0)
        ret = init_op_executor(align_resources);
    if (ret == 0) {
        if (align_resources.executor == nullptr) {
            std::fprintf(stderr, "%s align executor is null.\n", op_name());
            ret = -1;
        } else {
            ret = static_cast<int>(aclSetAclOpExecutorRepeatable(align_resources.executor));
        }
    }

    // 恢复被测 case 参数（case_shape/malloc_kSize 随后被测路径继续使用）
    data->data_size = saved_data_size;
    data->count = saved_count;
    case_shape = saved_shape;
    malloc_kSize = saved_malloc_kSize;

    if (ret != 0) {
        std::fprintf(stderr, "%s align op setup failed, rank=%d, ret=%d\n", op_name(), context->rank, ret);
        cleanup_align_op();
        return ret;
    }
    align_ready = true;
    return 0;
}

int HcclOpBaseTest::submit_align_op()
{
    if (!align_ready || align_resources.executor == nullptr)
        return -1;
    // 对齐轮提交到控制流：与 RecordNotify 同流保序（先同步后放行）。
    // execute_op 按 single-use 协议提交后摘除 executor；对齐轮 executor 为
    // repeatable，提交后恢复，供下一计时迭代复用。
    aclOpExecutor* repeatable = align_resources.executor;
    const int ret = execute_op(align_resources, context->control_stream);
    align_resources.executor = repeatable;
    return ret;
}

void HcclOpBaseTest::cleanup_align_op()
{
    align_resources.Reset();
    align_ready = false;
}

int HcclOpBaseTest::prepare_repeatable_executor()
{
    int ret = prepare_iteration_executor("repeatable", 0);
    if (ret != 0)
        return ret;

    ret = aclSetAclOpExecutorRepeatable(resources.executor);
    const int local_failed = ret == ACL_SUCCESS ? 0 : 1;
    int any_failed = 0;
    const int mpi_ret = MPI_Allreduce(&local_failed, &any_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    if (mpi_ret != MPI_SUCCESS) {
        resources.ResetExecutor();
        return mpi_ret;
    }
    if (any_failed == 0)
        return 0;

    resources.ResetExecutor();
    if (ret != ACL_SUCCESS) {
        std::fprintf(stderr, "%s repeatable executor setup failed, rank=%d, ret=%d.\n", op_name(), context->rank, ret);
        return ret;
    }
    if (context->rank == 0) {
        std::fprintf(stderr, "%s repeatable executor setup failed on another rank.\n", op_name());
    }
    return -1;
}

int HcclOpBaseTest::hccl_op_base_test()
{
    check_err = 0;
    int ret = init_buf_val(resources);
    if (ret != 0) {
        std::fprintf(stderr, "%s init_buf_val failed, rank=%d, ret=%d\n", op_name(), context->rank, ret);
        return ret;
    }

    const bool reuse_executor = options->accelerator == "ccu_sched";
    if (reuse_executor) {
        ret = prepare_repeatable_executor();
        if (ret != 0)
            return ret;
    }

    for (int iteration = 0; iteration < options->warmup_iters; ++iteration) {
        if (!reuse_executor) {
            ret = prepare_iteration_executor("warmup", iteration);
            if (ret != 0)
                return ret;
        }
        aclOpExecutor* repeatable_executor = resources.executor;
        ret = execute_op(resources, context->stream);
        if (reuse_executor)
            resources.executor = repeatable_executor;
        if (ret == ACL_SUCCESS && context->submitted)
            ret = context->submitted(false, iteration);
        AllreduceDeviceFailure(*context, false, "warmup task submission", ret);
    }

    ret = aclrtSynchronizeStream(context->stream);
    if (ret != ACL_SUCCESS) {
        const char* recent_error = aclGetRecentErrMsg();
        std::fprintf(
            stderr, "%s warmup stream synchronization failed, rank=%d, ret=%d, recent_error=%s\n", op_name(),
            context->rank, ret, recent_error == nullptr ? "<unavailable>" : recent_error);
        return ret;
    }
    // warmup 已做 in-place 修改，计时前恢复 input 初值（计时打点外，不影响性能数据）
    ret = reset_input();
    if (ret != 0)
        return ret;
    ret = aclrtSynchronizeStream(context->stream);
    if (ret != ACL_SUCCESS)
        return ret;
    MPI_Barrier(MPI_COMM_WORLD);

    std::vector<double> root_rank_time_us;
    if (context->rank == 0) {
        root_rank_time_us.resize(static_cast<size_t>(options->iters), 0.0);
    }
    std::vector<double> rank_spread_us(static_cast<size_t>(options->iters), 0.0);
    const bool collect_wait_time = reports_kernel_wait_time() && options->accelerator != "aicpu_ts";
    std::vector<uint64_t> root_rank_wait_ns;
    if (context->rank == 0 && collect_wait_time) {
        root_rank_wait_ns.resize(static_cast<size_t>(options->iters), 0);
    }
    if (options->sync_mode == "device") {
        // device 模式：与 mpi 模式唯一差异在放行机制。mpi 在 #1 allreduce
        // 确认全 rank 提交后由 host 直接 Record Notify 放行（放行点为 host
        // 时序）；device 改为先在控制流执行同算子 tiny 对齐轮再 Record
        // Notify（放行点 = 对齐轮完成点，设备侧对齐）。对齐轮是集合通信，
        // 全 rank 参与使其完成点即各 rank 对齐点，#1 之后的 host 提交离散
        // 被其吸收在计时窗外；notify 排在对齐轮之后同流保序，而各 rank 的
        // 主流 [wait][start][op][end] 在 #1 前已全部入队，gate 打开时三者
        // 背靠背执行、无 H2D 下发断流。失败传播与每迭代统计与 mpi 模式
        // 完全一致（AllreduceDeviceFailure / max-min allreduce）。
        ret = setup_align_op();
        const int local_align_failed = ret == 0 ? 0 : 1;
        int any_align_failed = 0;
        const int align_mpi_ret =
            MPI_Allreduce(&local_align_failed, &any_align_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        if (align_mpi_ret != MPI_SUCCESS || any_align_failed != 0) {
            if (local_align_failed == 0 && context->rank == 0) {
                std::fprintf(stderr, "%s align op setup failed on another rank.\n", op_name());
            }
            return -1;
        }

        for (int iteration = 0; iteration < options->iters; ++iteration) {
            if (!reuse_executor) {
                ret = prepare_iteration_executor("measured", iteration);
                if (ret != 0)
                    return ret;
            }
            bool gate_armed = false;
            int submission_ret = aclrtWaitAndResetNotify(
                context->gate_notify, context->stream, NotifyTimeoutSeconds(options->device_timeout_ms));
            if (submission_ret == ACL_SUCCESS) {
                gate_armed = true;
                submission_ret = aclrtRecordEvent(context->start_event, context->stream);
            }
            if (submission_ret == ACL_SUCCESS) {
                aclOpExecutor* repeatable_executor = resources.executor;
                submission_ret = execute_op(resources, context->stream);
                if (reuse_executor)
                    resources.executor = repeatable_executor;
                if (submission_ret == ACL_SUCCESS && context->submitted)
                    submission_ret = context->submitted(true, iteration);
            }
            if (submission_ret == ACL_SUCCESS) {
                submission_ret = aclrtRecordEvent(context->end_event, context->stream);
            }
            if (submission_ret != ACL_SUCCESS)
                resources.ResetExecutor();
            AllreduceDeviceFailure(*context, gate_armed, "task submission", submission_ret);

            // device 模式唯一差异：放行前先在控制流提交同算子 tiny 对齐轮，
            // Record Notify 排在其后同流保序（先同步后放行）。
            int release_ret = submit_align_op();
            if (release_ret == ACL_SUCCESS) {
                release_ret = aclrtRecordNotify(context->gate_notify, context->control_stream);
            }
            if (release_ret == ACL_SUCCESS) {
                release_ret = aclrtSynchronizeStreamWithTimeout(context->control_stream, options->device_timeout_ms);
            }
            AllreduceDeviceFailure(*context, gate_armed, "notify release", release_ret);

            int synchronize_ret = aclrtSynchronizeStreamWithTimeout(context->stream, options->device_timeout_ms);
            if (synchronize_ret == ACL_SUCCESS && context->completed)
                synchronize_ret = context->completed(iteration, resources);
            AllreduceDeviceFailure(*context, gate_armed, "main stream synchronization", synchronize_ret);

            if (!root_rank_wait_ns.empty()) {
                uint64_t wait_ns = 0;
                if ((context->read_wait ? context->read_wait(resources, wait_ns) :
                                          ReadKernelWaitTimeNs(resources.input.device_addr, wait_ns)) != 0) {
                    root_rank_wait_ns.clear();
                    std::fprintf(
                        stderr, "%s kernel wait-time report is unavailable, rank=%d, iteration=%d.\n", op_name(),
                        context->rank, iteration);
                } else {
                    root_rank_wait_ns[static_cast<size_t>(iteration)] = wait_ns;
                }
            }

            // in-place 算法（CCU NHR WriteReduce）在本轮已修改 input；end_event 已记录、
            // 流已同步，此处恢复初值不影响 start/end 打点区间，下一轮从干净状态开始。
            const int reset_ret = reset_input();
            AllreduceDeviceFailure(*context, gate_armed, "input reset for in-place algorithms", reset_ret);
            const int reset_sync_ret = aclrtSynchronizeStreamWithTimeout(context->stream, options->device_timeout_ms);
            AllreduceDeviceFailure(*context, gate_armed, "input reset synchronization", reset_sync_ret);

            float elapsed_ms = 0.0F;
            const int elapsed_ret = aclrtEventElapsedTime(&elapsed_ms, context->start_event, context->end_event);
            const int local_elapsed_error = elapsed_ret == ACL_SUCCESS ? 0 : 1;
            int elapsed_error = 0;
            MPI_Allreduce(&local_elapsed_error, &elapsed_error, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
            if (elapsed_error != 0) {
                if (elapsed_ret != ACL_SUCCESS) {
                    std::fprintf(
                        stderr, "%s event elapsed-time failed, rank=%d, iteration=%d, ret=%d.\n", op_name(),
                        context->rank, iteration, elapsed_ret);
                }
                return -1;
            }

            const double local_time_us = static_cast<double>(elapsed_ms) * 1000.0;
            if (context->timed)
                context->timed(iteration, local_time_us);
            double max_time_us = 0.0;
            double min_time_us = 0.0;
            MPI_Allreduce(&local_time_us, &max_time_us, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&local_time_us, &min_time_us, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            if (context->rank == 0) {
                root_rank_time_us[static_cast<size_t>(iteration)] = local_time_us;
            }
            rank_spread_us[static_cast<size_t>(iteration)] = max_time_us - min_time_us;
        }
    } else {
        // mpi 模式：既定基线流程——host 侧 allreduce 确认全 rank 提交后放行
        // gate，每迭代 allreduce 传播失败并汇聚 max/min 离散。
        for (int iteration = 0; iteration < options->iters; ++iteration) {
            if (!reuse_executor) {
                ret = prepare_iteration_executor("measured", iteration);
                if (ret != 0)
                    return ret;
            }
            bool gate_armed = false;
            int submission_ret = aclrtWaitAndResetNotify(
                context->gate_notify, context->stream, NotifyTimeoutSeconds(options->device_timeout_ms));
            if (submission_ret == ACL_SUCCESS) {
                gate_armed = true;
                submission_ret = aclrtRecordEvent(context->start_event, context->stream);
            }
            if (submission_ret == ACL_SUCCESS) {
                aclOpExecutor* repeatable_executor = resources.executor;
                submission_ret = execute_op(resources, context->stream);
                if (reuse_executor)
                    resources.executor = repeatable_executor;
                if (submission_ret == ACL_SUCCESS && context->submitted)
                    submission_ret = context->submitted(true, iteration);
            }
            if (submission_ret == ACL_SUCCESS) {
                submission_ret = aclrtRecordEvent(context->end_event, context->stream);
            }
            if (submission_ret != ACL_SUCCESS)
                resources.ResetExecutor();
            AllreduceDeviceFailure(*context, gate_armed, "task submission", submission_ret);

            int release_ret = aclrtRecordNotify(context->gate_notify, context->control_stream);
            if (release_ret == ACL_SUCCESS) {
                release_ret = aclrtSynchronizeStreamWithTimeout(context->control_stream, options->device_timeout_ms);
            }
            AllreduceDeviceFailure(*context, gate_armed, "notify release", release_ret);

            int synchronize_ret = aclrtSynchronizeStreamWithTimeout(context->stream, options->device_timeout_ms);
            if (synchronize_ret == ACL_SUCCESS && context->completed)
                synchronize_ret = context->completed(iteration, resources);
            AllreduceDeviceFailure(*context, gate_armed, "main stream synchronization", synchronize_ret);

            if (!root_rank_wait_ns.empty()) {
                uint64_t wait_ns = 0;
                if ((context->read_wait ? context->read_wait(resources, wait_ns) :
                                          ReadKernelWaitTimeNs(resources.input.device_addr, wait_ns)) != 0) {
                    root_rank_wait_ns.clear();
                    std::fprintf(
                        stderr, "%s kernel wait-time report is unavailable, rank=%d, iteration=%d.\n", op_name(),
                        context->rank, iteration);
                } else {
                    root_rank_wait_ns[static_cast<size_t>(iteration)] = wait_ns;
                }
            }

            // in-place 算法（CCU NHR WriteReduce）在本轮已修改 input；end_event 已记录、
            // 流已同步，此处恢复初值不影响 start/end 打点区间，下一轮从干净状态开始。
            const int reset_ret = reset_input();
            AllreduceDeviceFailure(*context, gate_armed, "input reset for in-place algorithms", reset_ret);
            const int reset_sync_ret = aclrtSynchronizeStreamWithTimeout(context->stream, options->device_timeout_ms);
            AllreduceDeviceFailure(*context, gate_armed, "input reset synchronization", reset_sync_ret);

            float elapsed_ms = 0.0F;
            const int elapsed_ret = aclrtEventElapsedTime(&elapsed_ms, context->start_event, context->end_event);
            const int local_elapsed_error = elapsed_ret == ACL_SUCCESS ? 0 : 1;
            int elapsed_error = 0;
            MPI_Allreduce(&local_elapsed_error, &elapsed_error, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
            if (elapsed_error != 0) {
                if (elapsed_ret != ACL_SUCCESS) {
                    std::fprintf(
                        stderr, "%s event elapsed-time failed, rank=%d, iteration=%d, ret=%d.\n", op_name(),
                        context->rank, iteration, elapsed_ret);
                }
                return -1;
            }

            const double local_time_us = static_cast<double>(elapsed_ms) * 1000.0;
            if (context->timed)
                context->timed(iteration, local_time_us);
            double max_time_us = 0.0;
            double min_time_us = 0.0;
            MPI_Allreduce(&local_time_us, &max_time_us, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&local_time_us, &min_time_us, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            if (context->rank == 0) {
                root_rank_time_us[static_cast<size_t>(iteration)] = local_time_us;
            }
            rank_spread_us[static_cast<size_t>(iteration)] = max_time_us - min_time_us;
        }
    }

    const int local_check = check_buf_result();
    MPI_Allreduce(&local_check, &check_err, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    ret = print_execution_time(root_rank_time_us, rank_spread_us, root_rank_wait_ns);
    MPI_Barrier(MPI_COMM_WORLD);
    return check_err == 0 && ret == 0 ? 0 : -1;
}

int HcclOpBaseTest::print_execution_time(
    const std::vector<double>& root_rank_time_us, const std::vector<double>& rank_spread_us,
    const std::vector<uint64_t>& root_rank_wait_ns)
{
    int local_statistics_error = 0;
    if (context->rank == 0) {
        if (root_rank_time_us.empty() || root_rank_time_us.size() != rank_spread_us.size()) {
            std::fprintf(stderr, "%s timing statistics are empty or inconsistent.\n", op_name());
            local_statistics_error = 1;
        }
        for (size_t index = 0; local_statistics_error == 0 && index < root_rank_time_us.size(); ++index) {
            if (!std::isfinite(root_rank_time_us[index]) || root_rank_time_us[index] < 0.0 ||
                !std::isfinite(rank_spread_us[index]) || rank_spread_us[index] < 0.0) {
                std::fprintf(
                    stderr, "%s timing statistics contain an invalid value at iteration %zu.\n", op_name(), index);
                local_statistics_error = 1;
            }
        }
        if (local_statistics_error == 0 && !root_rank_wait_ns.empty() &&
            root_rank_wait_ns.size() != root_rank_time_us.size()) {
            std::fprintf(stderr, "%s kernel wait-time statistics are inconsistent with event statistics.\n", op_name());
            local_statistics_error = 1;
        }

        if (local_statistics_error == 0) {
            double total_time_us = 0.0;
            double total_spread_us = 0.0;
            for (size_t index = 0; index < root_rank_time_us.size(); ++index) {
                total_time_us += root_rank_time_us[index];
                total_spread_us += rank_spread_us[index];
            }
            const double mean_time_us = total_time_us / static_cast<double>(root_rank_time_us.size());
            const double mean_spread_us = total_spread_us / static_cast<double>(rank_spread_us.size());
            std::vector<double> sorted_time_us = root_rank_time_us;
            std::sort(sorted_time_us.begin(), sorted_time_us.end());
            const size_t p50_index =
                static_cast<size_t>(std::ceil(0.50 * static_cast<double>(sorted_time_us.size()))) - 1U;
            const size_t p95_index =
                static_cast<size_t>(std::ceil(0.95 * static_cast<double>(sorted_time_us.size()))) - 1U;
            const double p50_time_us = sorted_time_us[p50_index];
            const double p95_time_us = sorted_time_us[p95_index];

            // 有内核 Wait 打点时，mean/p50/p95 展示新 API 通信耗时（hccl.Wait
            // 实测），api_framework_time 为原 ACL Event 耗时减去新通信耗时。
            const bool wait_available =
                !root_rank_wait_ns.empty() && root_rank_wait_ns.size() == root_rank_time_us.size();
            uint64_t total_wait_ns = 0;
            std::vector<uint64_t> sorted_wait_ns;
            if (wait_available) {
                sorted_wait_ns = root_rank_wait_ns;
                std::sort(sorted_wait_ns.begin(), sorted_wait_ns.end());
                for (size_t index = 0; index < sorted_wait_ns.size(); ++index) {
                    total_wait_ns += sorted_wait_ns[index];
                }
            }
            const double iteration_count = static_cast<double>(root_rank_time_us.size());
            const double mean_comm_time_us =
                wait_available ? static_cast<double>(total_wait_ns) / 1000.0 / iteration_count : mean_time_us;
            const double p50_comm_time_us =
                wait_available ? static_cast<double>(sorted_wait_ns[p50_index]) / 1000.0 : p50_time_us;
            const double p95_comm_time_us =
                wait_available ? static_cast<double>(sorted_wait_ns[p95_index]) / 1000.0 : p95_time_us;
            const double framework_time_us = wait_available ? mean_time_us - mean_comm_time_us : 0.0;
            const double algorithm_bandwidth_gbytes_s =
                mean_comm_time_us > 0.0 ? static_cast<double>(case_shape.data_bytes) / mean_comm_time_us * 1.0e-3 : 0.0;

            if (print_header) {
                if (wait_available) {
                    std::printf(
                        "%-18s | %-14s | %-14s | %-14s | %-20s | %-22s | %-22s | %s\n", "data_size(Bytes)",
                        "mean_time(us)", "p50_time(us)", "p95_time(us)", "mean_rank_spread(us)",
                        "api_framework_time(us)", "alg_bandwidth(GB/s)", "check_result");
                } else {
                    std::printf(
                        "%-18s | %-14s | %-14s | %-14s | %-20s | %-22s | %s\n", "data_size(Bytes)", "mean_time(us)",
                        "p50_time(us)", "p95_time(us)", "mean_rank_spread(us)", "alg_bandwidth(GB/s)", "check_result");
                }
                print_header = false;
            }
            const char* check_text = options->check == 0 ? "NULL" : (check_err == 0 ? "success" : "failed");
            if (wait_available) {
                std::printf(
                    "%-18llu | %-14.2f | %-14.2f | %-14.2f | %-20.2f | %-22.2f | %-22.5f | %s\n",
                    static_cast<unsigned long long>(case_shape.requested_bytes), mean_comm_time_us, p50_comm_time_us,
                    p95_comm_time_us, mean_spread_us, framework_time_us, algorithm_bandwidth_gbytes_s, check_text);
            } else {
                std::printf(
                    "%-18llu | %-14.2f | %-14.2f | %-14.2f | %-20.2f | %-22.5f | %s\n",
                    static_cast<unsigned long long>(case_shape.requested_bytes), mean_time_us, p50_time_us, p95_time_us,
                    mean_spread_us, algorithm_bandwidth_gbytes_s, check_text);
            }
        }
    }

    int statistics_error = 0;
    const int mpi_ret = MPI_Allreduce(&local_statistics_error, &statistics_error, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    return mpi_ret == MPI_SUCCESS && statistics_error == 0 ? 0 : -1;
}

int HcclTest::opbase_test_by_data_size()
{
    if (context == nullptr || options == nullptr || dtype_info == nullptr)
        return -1;
    if (context->rank == 0) {
        std::printf(
            "op=%s datatype=%s ranks=%d npus_per_node=%d accelerator=%s "
            "sync=%s iters=%d warmup=%d time_mode=device\n",
            op_name(), dtype_info->name, context->rank_size, options->npus, options->accelerator.c_str(),
            options->sync_mode.c_str(), options->iters, options->warmup_iters);
    }

    uint64_t current = data->min_bytes;
    while (current <= data->max_bytes) {
        data->data_size = current;
        int ret = get_buff_size();
        if (ret == 0 && context->shape_ready)
            ret = context->shape_ready(case_shape);
        if (ret == 0)
            ret = hccl_op_base_test();
        resources.Reset();
        const int status = SynchronizeDataSizeStatus(*context, ret, current);
        if (status != 0) {
            if (context->rank == 0) {
                std::fprintf(
                    stderr, "%s test failed for %llu bytes, ret=%d.\n", op_name(),
                    static_cast<unsigned long long>(current), status);
            }
            return status;
        }
        uint64_t next = 0;
        if (data->step_factor > 1.0) {
            const long double candidate = static_cast<long double>(current) * data->step_factor;
            if (candidate >= std::ldexp(1.0L, 64))
                break;
            next = static_cast<uint64_t>(candidate);
        } else {
            if (current > std::numeric_limits<uint64_t>::max() - data->step_bytes)
                break;
            next = current + data->step_bytes;
        }
        if (next <= current)
            break;
        current = next;
    }
    return 0;
}

} // namespace hccl_api_test
