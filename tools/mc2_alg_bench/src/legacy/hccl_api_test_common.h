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
// Original source: common/hccl_api_test_common.h

#ifndef HCCL_API_TEST_COMMON_H
#define HCCL_API_TEST_COMMON_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "aclnn/acl_meta.h"
#include "hccl/hccl.h"

namespace hccl_api_test {

struct Options {
    uint64_t min_bytes = 64ULL * 1024 * 1024;
    uint64_t max_bytes = 64ULL * 1024 * 1024;
    uint64_t step_bytes = 0;
    double step_factor = 1.0;
    int iters = 20;
    int warmup_iters = 10;
    int device_timeout_ms = 60000;
    int check = 1;
    int npus = -1;
    std::string datatype = "fp32";
    std::string reduce_op = "sum";
    std::string accelerator = "aicpu_ts";
    std::string algorithm;
    uint32_t accelerator_mode = 2U;
    // 计量迭代 rank 同步方式：device = 双流 gate（控制流同算子 tiny 对齐轮
    // 完成后放行）；mpi = 原流程（host 侧 MPI allreduce 同步后放行）。仅对
    // 本仓 api 路径二进制生效，hccl 基线路径为外部标准工具不受影响。
    std::string sync_mode = "mpi";
    bool step_bytes_set = false;
    bool step_factor_set = false;
};

struct DataSize {
    uint64_t min_bytes = 0;
    uint64_t max_bytes = 0;
    uint64_t step_bytes = 0;
    uint64_t data_size = 0;
    uint64_t count = 0;
    size_t type_size = 0;
    double step_factor = 1.0;
};

struct DTypeInfo {
    const char* name;
    aclDataType acl_type;
    size_t size;
    bool zero_only_init;
};

struct CaseResources;
struct CaseShape;

struct RuntimeContext {
    int rank = 0;
    int rank_size = 0;
    int device_id = 0;
    aclrtStream stream = nullptr;
    aclrtStream control_stream = nullptr;
    aclrtNotify gate_notify = nullptr;
    aclrtEvent start_event = nullptr;
    aclrtEvent end_event = nullptr;
    HcclComm comm = nullptr;
    char comm_name[128] = {0};
    // Boundary observers supply structured facts; the legacy loop owns timing.
    std::function<int(bool, int)> submitted;
    std::function<int(int, const CaseResources&)> completed;
    std::function<void(int, double)> timed;
    std::function<int(const CaseResources&, uint64_t&)> read_wait;
    std::function<int(const CaseShape&)> shape_ready;
    std::function<void(const char*, int)> fatal;
};

struct DeviceTensor {
    void* device_addr = nullptr;
    aclTensor* tensor = nullptr;
    size_t bytes = 0;

    void Reset();
};

struct CaseShape {
    uint64_t requested_bytes = 0;
    uint64_t input_count = 0;
    uint64_t output_count = 0;
    uint64_t input_bytes = 0;
    uint64_t output_bytes = 0;
    uint64_t data_bytes = 0;
    uint64_t per_peer_count = 0;
    int64_t tile_m = 0;
    int64_t tile_k = 0;
    std::vector<int64_t> input_shape;
    std::vector<int64_t> output_shape;
};

struct CaseResources {
    DeviceTensor input;
    DeviceTensor output;
    DeviceTensor input_splits;
    DeviceTensor output_splits;
    void* workspace = nullptr;
    uint64_t workspace_size = 0;
    uint64_t workspace_capacity = 0;
    aclOpExecutor* executor = nullptr;

    void ResetExecutor();
    void Reset();
};

class HcclTest {
private:
    DataSize data_storage_;

public:
    HcclTest();
    virtual ~HcclTest() = default;

    HcclTest(const HcclTest&) = delete;
    HcclTest& operator=(const HcclTest&) = delete;

    virtual const char* op_name() const = 0;
    virtual bool is_reduction() const { return false; }
    virtual int hccl_op_base_test() = 0;

    void set_test_context(RuntimeContext& runtime_context, const Options& test_options, const DTypeInfo& dtype);
    int opbase_test_by_data_size();

    DataSize* data = nullptr;
    size_t malloc_kSize = 0;
    bool need_ranksize_alignment = false;

protected:
    virtual void init_data_count();
    virtual size_t init_malloc_Ksize_by_data() = 0;
    virtual void init_send_recv_size_by_data(size_t& send_bytes, size_t& recv_bytes) = 0;
    virtual bool init_case_shape() = 0;
    int get_buff_size();

    RuntimeContext* context = nullptr;
    const Options* options = nullptr;
    const DTypeInfo* dtype_info = nullptr;
    CaseShape case_shape;
    CaseResources resources;
};

class HcclOpBaseTest : public HcclTest {
public:
    int hccl_op_base_test() override;
    bool validation_failed() const { return check_err != 0; }
    ~HcclOpBaseTest() override;

protected:
    // init_buf_val/init_op_executor/execute_op 均以资源集为参数：对齐轮与被测轮
    // 各持一套 CaseResources，走完全相同的算子构建路径。
    // init_buf_val 仅在被测资源集（&res == &resources）时更新 input_seed——
    // 对齐轮数据内容不参与校验，且不得覆盖被测 case 的 reset 基线。
    virtual int init_buf_val(CaseResources& res);
    virtual int init_op_executor(CaseResources& res) = 0;
    virtual int execute_op(CaseResources& res, aclrtStream stream) = 0;
    virtual std::vector<uint8_t> build_expected() const = 0;
    virtual bool reports_kernel_wait_time() const { return false; }

    // 对齐轮：与被测算子完全相同的算子在 tiny 数据尺寸（16B 下限，实际形态由
    // 各算子自身最小 case 逻辑决定）下执行一遍——临时把 data->data_size 切到
    // tiny 走 get_buff_size/init_buf_val/init_op_executor 自身路径，executor 置
    // repeatable，不写死任何算子。集合通信算子天然要求全 rank 参与，其完成点
    // 即各 rank 对齐点，随后的被测轮从对齐状态起跑：host 提交离散与循环内簿记
    // 耗时被吸收在对齐轮内（计时窗外）。一次构建（单 dtype 运行），析构期释放。
    int setup_align_op();
    int submit_align_op();
    void cleanup_align_op();

    int prepare_iteration_executor(const char* phase, int iteration);
    int prepare_repeatable_executor();
    virtual int check_buf_result();
    int print_execution_time(
        const std::vector<double>& root_rank_time_us, const std::vector<double>& rank_spread_us,
        const std::vector<uint64_t>& root_rank_wait_ns);
    // 将 input 恢复为 init_buf_val 时的初始值（H2D 重拷贝）。
    // in-place 算法（如 CCU NHR ReduceScatter 的 WriteReduce）会在 op 间修改
    // input buffer 并残留累积值，重复调用前必须重置；调用点放在计时打点之外，
    // 不影响 start_event/end_event 区间的性能数据。
    virtual int reset_input();

    int check_err = 0;
    bool print_header = true;
    std::vector<uint8_t> input_seed; // init_buf_val 时 input 的 host 侧初始副本
    CaseResources align_resources;   // 对齐轮：同算子 tiny 形态的独立资源集
    bool align_ready = false;
};

void AlignOptionsLikeHccl(Options& options, const HcclTest& opbase, int rank_size);
const DTypeInfo* FindDType(const std::string& name);

bool BytesToElementCount(uint64_t requested_bytes, const DTypeInfo& dtype, CaseShape& shape, uint64_t& count);
bool AlignElementCount(uint64_t& count, uint64_t alignment);
bool AlignAllToAllElementCount(uint64_t& count, int rank_size);
bool FinalizeCaseShape(const DTypeInfo& dtype, bool data_size_uses_output, CaseShape& shape);

int ReduceOpValue(const std::string& name);
int CreateDeviceTensor(
    const std::vector<int64_t>& shape, aclDataType dtype, size_t element_size, const void* host_data,
    DeviceTensor& result, aclrtMemMallocPolicy policy = ACL_MEM_MALLOC_HUGE_FIRST, size_t min_allocation_bytes = 0);
int EnsureWorkspace(RuntimeContext& context, uint64_t required, CaseResources& resources);
int PrepareSingleUseExecutor(
    RuntimeContext& context, uint64_t workspace_size, aclOpExecutor* executor, CaseResources& resources);

std::vector<uint8_t> BuildSourceOrderedExpected(
    const RuntimeContext& context, const DTypeInfo& dtype, const CaseShape& shape, uint64_t block_count);
std::vector<uint8_t> BuildReductionExpected(
    const RuntimeContext& context, const Options& options, const DTypeInfo& dtype, const CaseShape& shape);

} // namespace hccl_api_test

#endif
