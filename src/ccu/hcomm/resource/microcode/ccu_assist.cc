/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode/ccu_assist_v1.h"

#include <map>

#include "hcomm/resource/microcode/ccu_microcode_v1.h"

#include "runtime/rt_external_device.h"

#include "hcomm/common/ccu_exception.h"

namespace asc {
namespace ccu_rep {

namespace {
// UB 内存访问凭证查询结构。runtime 的 rtUbDevQueryInfo 以 void* 收参、不提供该类型定义，
// 故由调用方按 runtime 约定的布局本地声明；字段顺序与宽度不可改动。
struct asc_ub_token_info {
    uint64_t va;
    uint64_t size;
    uint32_t token_id;
    uint32_t token_value;
};

// 仅用于异常信息，hccl_types.h 未提供枚举名转字符串能力
std::string describe_data_type(HcclDataType data_type)
{
    switch (data_type) {
        case HCCL_DATA_TYPE_INT8:
            return "INT8";
        case HCCL_DATA_TYPE_INT16:
            return "INT16";
        case HCCL_DATA_TYPE_INT32:
            return "INT32";
        case HCCL_DATA_TYPE_FP16:
            return "FP16";
        case HCCL_DATA_TYPE_FP32:
            return "FP32";
        case HCCL_DATA_TYPE_INT64:
            return "INT64";
        case HCCL_DATA_TYPE_UINT64:
            return "UINT64";
        case HCCL_DATA_TYPE_UINT8:
            return "UINT8";
        case HCCL_DATA_TYPE_UINT16:
            return "UINT16";
        case HCCL_DATA_TYPE_UINT32:
            return "UINT32";
        case HCCL_DATA_TYPE_FP64:
            return "FP64";
        case HCCL_DATA_TYPE_BFP16:
            return "BFP16";
        case HCCL_DATA_TYPE_INT128:
            return "INT128";
        case HCCL_DATA_TYPE_HIF8:
            return "HIF8";
        case HCCL_DATA_TYPE_FP8E4M3:
            return "FP8E4M3";
        case HCCL_DATA_TYPE_FP8E5M2:
            return "FP8E5M2";
        case HCCL_DATA_TYPE_FP8E8M0:
            return "FP8E8M0";
        default:
            return "Invalid(" + std::to_string(static_cast<int32_t>(data_type)) + ")";
    }
}

std::string describe_reduce_op(HcclReduceOp reduce_op)
{
    switch (reduce_op) {
        case HCCL_REDUCE_SUM:
            return "SUM";
        case HCCL_REDUCE_PROD:
            return "PROD";
        case HCCL_REDUCE_MAX:
            return "MAX";
        case HCCL_REDUCE_MIN:
            return "MIN";
        default:
            return "Invalid(" + std::to_string(static_cast<int32_t>(reduce_op)) + ")";
    }
}
} // namespace

constexpr uint64_t set_bits(uint16_t start, uint16_t end)
{
    return ((uint64_t(1) << (end - start + 1)) - uint64_t(1)) << start;
}

constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

uint64_t get_loop_param(uint64_t loop_ctx_id, uint64_t gsa_offset, uint64_t loop_iter_num)
{
    constexpr uint16_t ctx_id_bit_num = 8;
    constexpr uint16_t ctx_id_shift_bit = 45;
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 13;
    constexpr uint16_t loop_num_bit_num = 13;
    constexpr uint16_t loop_num_shift_bit = 0;
    return ((loop_ctx_id & set_bits(ctx_id_bit_num)) << ctx_id_shift_bit) |
           ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((loop_iter_num & set_bits(loop_num_bit_num)) << loop_num_shift_bit);
}

uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & set_bits(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & set_bits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & set_bits(total_loop_bit_num)) << total_loop_shift_bit);
}

uint64_t get_parallel_param_v2(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t loop_num_bit_num = 10;
    constexpr uint16_t loop_num_shift_bit = 0;
    constexpr uint16_t repeat_loop_bit_num = 9;
    constexpr uint16_t repeat_loop_shift_bit = 10;
    constexpr uint16_t extend_bit_num = 9;
    constexpr uint16_t extend_shift_bit = 19;
    return ((total_loop_num & set_bits(loop_num_bit_num)) << loop_num_shift_bit) |
           ((repeat_loop_index & set_bits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((repeat_num & set_bits(extend_bit_num)) << extend_shift_bit);
}

uint64_t get_offset_param(uint64_t gsa_offset, uint64_t ms_offset, uint64_t cke_offset)
{
    constexpr uint16_t gsa_bit_num = 32;
    constexpr uint16_t gsa_shift_bit = 21;
    constexpr uint16_t ms_bit_num = 11;
    constexpr uint16_t ms_shift_bit = 10;
    constexpr uint16_t cke_bit_num = 10;
    constexpr uint16_t cke_shift_bit = 0;
    return ((gsa_offset & set_bits(gsa_bit_num)) << gsa_shift_bit) |
           ((ms_offset & set_bits(ms_bit_num)) << ms_shift_bit) |
           ((cke_offset & set_bits(cke_bit_num)) << cke_shift_bit);
}

uint64_t get_token(uint64_t token_id, uint64_t token_value, uint64_t token_valid)
{
    constexpr uint16_t token_valid_bit_num = 1;
    constexpr uint16_t token_valid_shift_bit = 52;
    constexpr uint16_t token_id_bit_num = 20;
    constexpr uint16_t token_id_shift_bit = 32;
    constexpr uint16_t token_value_bit_num = 32;
    constexpr uint16_t token_value_shift_bit = 0;
    return ((token_valid & set_bits(token_valid_bit_num)) << token_valid_shift_bit) |
           ((token_id & set_bits(token_id_bit_num)) << token_id_shift_bit) |
           ((token_value & set_bits(token_value_bit_num)) << token_value_shift_bit);
}

uint64_t ccu_combine_token_info(uint64_t token_id, uint64_t token_value, uint64_t token_valid)
{
    return get_token(token_id, token_value, token_valid);
}

uint16_t get_ccu_reduce_type(HcclReduceOp reduce_op)
{
    static std::map<HcclReduceOp, uint16_t> ccu_reduce_type_map = {
        {HCCL_REDUCE_SUM, ccu_reduce_sum},
        {HCCL_REDUCE_MAX, ccu_reduce_max},
        {HCCL_REDUCE_MIN, ccu_reduce_min},
    };

    if (ccu_reduce_type_map.find(reduce_op) == ccu_reduce_type_map.end()) {
        asc::throw_ccu_internal("Unsupported ReduceOp[%s] for Ccu", describe_reduce_op(reduce_op).c_str());
    }

    return ccu_reduce_type_map[reduce_op];
}

uint16_t get_ccu_data_type(HcclDataType data_type, HcclReduceOp reduce_op)
{
    static std::map<HcclDataType, uint16_t> ccu_sum_data_type_map = {
        {HCCL_DATA_TYPE_FP32, 0},    {HCCL_DATA_TYPE_FP16, 1},    {HCCL_DATA_TYPE_BFP16, 2}, {HCCL_DATA_TYPE_HIF8, 3},
        {HCCL_DATA_TYPE_FP8E4M3, 4}, {HCCL_DATA_TYPE_FP8E5M2, 5}, {HCCL_DATA_TYPE_INT8, 6},  {HCCL_DATA_TYPE_UINT8, 7},
        {HCCL_DATA_TYPE_INT16, 8},   {HCCL_DATA_TYPE_INT32, 9},
    };

    static std::map<HcclDataType, uint16_t> ccu_max_min_data_type_map = {
        {HCCL_DATA_TYPE_FP32, 0},  {HCCL_DATA_TYPE_FP16, 1},  {HCCL_DATA_TYPE_BFP16, 2}, {HCCL_DATA_TYPE_INT8, 6},
        {HCCL_DATA_TYPE_UINT8, 7}, {HCCL_DATA_TYPE_INT16, 8}, {HCCL_DATA_TYPE_INT32, 9},

    };

    uint16_t ccu_reduce_type = get_ccu_reduce_type(reduce_op);
    if (ccu_reduce_type == ccu_reduce_sum) {
        if (ccu_sum_data_type_map.find(data_type) == ccu_sum_data_type_map.end()) {
            asc::throw_ccu_internal("Unsupported DataType[%s] for Ccu SUM", describe_data_type(data_type).c_str());
        }
        return ccu_sum_data_type_map[data_type];
    }

    if (ccu_reduce_type == ccu_reduce_max || ccu_reduce_type == ccu_reduce_min) {
        if (ccu_max_min_data_type_map.find(data_type) == ccu_max_min_data_type_map.end()) {
            asc::throw_ccu_internal("Unsupported DataType[%s] for Ccu MAX/MIN", describe_data_type(data_type).c_str());
        }
        return ccu_max_min_data_type_map[data_type];
    }

    return ccu_sum_data_type_map[data_type];
}

uint16_t get_ub_reduce_type(HcclReduceOp reduce_op)
{
    static std::map<HcclReduceOp, uint16_t> ub_reduce_type_map = {
        {HCCL_REDUCE_SUM, 10},
        {HCCL_REDUCE_MAX, 8},
        {HCCL_REDUCE_MIN, 9},
    };

    if (ub_reduce_type_map.find(reduce_op) == ub_reduce_type_map.end()) {
        asc::throw_ccu_internal("Unsupported reduceOp[%s] for UB Reduce", describe_reduce_op(reduce_op).c_str());
    }

    return ub_reduce_type_map[reduce_op];
}

uint16_t get_ub_data_type(HcclDataType data_type)
{
    static std::map<HcclDataType, uint16_t> ub_data_type_map = {
        {HCCL_DATA_TYPE_FP32, 7},  {HCCL_DATA_TYPE_FP16, 6},   {HCCL_DATA_TYPE_BFP16, 8},
        {HCCL_DATA_TYPE_INT8, 0},  {HCCL_DATA_TYPE_UINT8, 3},  {HCCL_DATA_TYPE_INT16, 1},
        {HCCL_DATA_TYPE_INT32, 2}, {HCCL_DATA_TYPE_UINT16, 4}, {HCCL_DATA_TYPE_UINT32, 5}};

    if (ub_data_type_map.find(data_type) == ub_data_type_map.end()) {
        asc::throw_ccu_internal("Unsupported DataType[%s] for UB Reduce", describe_data_type(data_type).c_str());
    }
    return ub_data_type_map[data_type];
}

uint64_t get_token_info(uint64_t va, uint64_t size)
{
    // runtime 的 rtUbDevQueryInfo 以 void* 收取查询结构，布局由调用方按约定解释。
    asc_ub_token_info info{};
    info.va = va;
    info.size = size;
    if (rtUbDevQueryInfo(QUERY_PROCESS_TOKEN, &info) != RT_ERROR_NONE) {
        // token 信息属于安全信息，失败时不打印 va/size 之外的内容
        asc::throw_ccu_internal("failed to query tokenInfo.");
    }
    // runtime 返回的 tokenId 低 8 位非 CCU 所用语义，需右移后再写入 token 寄存器位域
    constexpr uint32_t token_id_shift_bit = 8;
    info.token_id >>= token_id_shift_bit;
    return ccu_rep::get_token(info.token_id, info.token_value, 1);
}

}; // namespace ccu_rep
}; // namespace asc
