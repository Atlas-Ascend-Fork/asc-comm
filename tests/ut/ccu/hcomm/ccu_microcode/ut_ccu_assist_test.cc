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
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "ccu_api_exception.h"
#include <gtest/gtest.h>

#include "ccu/hcomm/ccu_utils.hpp"

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
namespace ccu_rep {
namespace {

class CcuAssistTest : public ::testing::Test {};

TEST_F(CcuAssistTest, get_loop_param)
{
    uint64_t loop_ctx_id = 5;
    uint64_t gsa_offset = 100;
    uint64_t loop_iter_num_ = 200;
    uint64_t result = get_loop_param(loop_ctx_id, gsa_offset, loop_iter_num_);

    EXPECT_GT(result, 0);
}

TEST_F(CcuAssistTest, GetLoopParam_ZeroInputs)
{
    uint64_t result = get_loop_param(0, 0, 0);
    EXPECT_EQ(result, 0);
}

TEST_F(CcuAssistTest, get_token)
{
    uint64_t token_id = 10;
    uint64_t token_value = 0xABCD;
    uint64_t token_valid = 1;
    uint64_t result = get_token(token_id, token_value, token_valid);

    EXPECT_GT(result, 0);
}

TEST_F(CcuAssistTest, GetToken_ValidZero)
{
    uint64_t result = get_token(0, 0, 0);
    EXPECT_EQ(result, 0);
}

TEST_F(CcuAssistTest, GetTokenInfo_EncodesQueriedToken) { EXPECT_EQ(get_token_info(0, 0), 1ULL << 52); }

TEST_F(CcuAssistTest, GetCcuReduceType_SUM)
{
    uint16_t result = get_ccu_reduce_type(HCCL_REDUCE_SUM);
    EXPECT_EQ(result, ccu_reduce_sum);
}

TEST_F(CcuAssistTest, GetCcuReduceType_MAX)
{
    uint16_t result = get_ccu_reduce_type(HCCL_REDUCE_MAX);
    EXPECT_EQ(result, ccu_reduce_max);
}

TEST_F(CcuAssistTest, GetCcuReduceType_MIN)
{
    uint16_t result = get_ccu_reduce_type(HCCL_REDUCE_MIN);
    EXPECT_EQ(result, ccu_reduce_min);
}

TEST_F(CcuAssistTest, GetCcuReduceType_Unsupported)
{
    EXPECT_THROW(get_ccu_reduce_type(HCCL_REDUCE_PROD), CcuUtException);
}

TEST_F(CcuAssistTest, GetCcuDataType_FP32_SUM)
{
    uint16_t result = get_ccu_data_type(HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM);
    EXPECT_EQ(result, 0);
}

TEST_F(CcuAssistTest, GetCcuDataType_FP16_SUM)
{
    uint16_t result = get_ccu_data_type(HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM);
    EXPECT_EQ(result, 1);
}

TEST_F(CcuAssistTest, GetCcuDataType_INT32_SUM)
{
    uint16_t result = get_ccu_data_type(HCCL_DATA_TYPE_INT32, HCCL_REDUCE_SUM);
    EXPECT_EQ(result, 9);
}

TEST_F(CcuAssistTest, GetCcuDataType_FP32_MAX)
{
    uint16_t result = get_ccu_data_type(HCCL_DATA_TYPE_FP32, HCCL_REDUCE_MAX);
    EXPECT_EQ(result, 0);
}

TEST_F(CcuAssistTest, GetCcuDataType_INT32_MAX)
{
    uint16_t result = get_ccu_data_type(HCCL_DATA_TYPE_INT32, HCCL_REDUCE_MAX);
    EXPECT_EQ(result, 9);
}

TEST_F(CcuAssistTest, GetCcuDataType_UnsupportedType_SUM)
{
    EXPECT_THROW(get_ccu_data_type(HCCL_DATA_TYPE_RESERVED, HCCL_REDUCE_SUM), CcuUtException);
}

TEST_F(CcuAssistTest, GetCcuDataType_UnsupportedType_MAX)
{
    EXPECT_THROW(
        get_ccu_data_type(static_cast<HcclDataType>(HCCL_DATA_TYPE_FP8E5M2 + 1), HCCL_REDUCE_MAX), CcuUtException);
}

TEST_F(CcuAssistTest, GetUBReduceType_SUM)
{
    uint16_t result = get_ub_reduce_type(HCCL_REDUCE_SUM);
    EXPECT_EQ(result, 10);
}

TEST_F(CcuAssistTest, GetUBReduceType_MAX)
{
    uint16_t result = get_ub_reduce_type(HCCL_REDUCE_MAX);
    EXPECT_EQ(result, 8);
}

TEST_F(CcuAssistTest, GetUBReduceType_MIN)
{
    uint16_t result = get_ub_reduce_type(HCCL_REDUCE_MIN);
    EXPECT_EQ(result, 9);
}

TEST_F(CcuAssistTest, GetUBReduceType_Unsupported)
{
    EXPECT_THROW(get_ub_reduce_type(HCCL_REDUCE_PROD), CcuUtException);
}

TEST_F(CcuAssistTest, GetUBDataType_FP32)
{
    uint16_t result = get_ub_data_type(HCCL_DATA_TYPE_FP32);
    EXPECT_EQ(result, 7);
}

TEST_F(CcuAssistTest, GetUBDataType_FP16)
{
    uint16_t result = get_ub_data_type(HCCL_DATA_TYPE_FP16);
    EXPECT_EQ(result, 6);
}

TEST_F(CcuAssistTest, GetUBDataType_INT32)
{
    uint16_t result = get_ub_data_type(HCCL_DATA_TYPE_INT32);
    EXPECT_EQ(result, 2);
}

TEST_F(CcuAssistTest, GetUBDataType_Unsupported)
{
    EXPECT_THROW(get_ub_data_type(HCCL_DATA_TYPE_RESERVED), CcuUtException);
}

TEST_F(CcuAssistTest, GetParallelParamV2_EncodesFields)
{
    uint64_t result = get_parallel_param_v2(/*repeat_num=*/3, /*repeat_loop_index=*/2, /*total_loop_num=*/7);

    // 位域布局: total_loop_num 占低 10 位, repeat_loop_index 占第 10~18 位, repeat_num 占第 19~27 位
    EXPECT_EQ(result & 0x3FF, 7u);
    EXPECT_EQ((result >> 10) & 0x1FF, 2u);
    EXPECT_EQ((result >> 19) & 0x1FF, 3u);
}

TEST_F(CcuAssistTest, GetParallelParamV2_ZeroInputs) { EXPECT_EQ(get_parallel_param_v2(0, 0, 0), 0); }

TEST_F(CcuAssistTest, GetParallelParamV2_SaturatesEachField)
{
    // 各字段超位宽时按掩码截断: repeat_loop_index 传 10 位值 0x3FF, 9 位字段只保留低 9 位
    uint64_t result = get_parallel_param_v2(0x1FF, 0x3FF, 0x1FF);
    EXPECT_EQ(result & 0x3FF, 0x1FF);
    EXPECT_EQ((result >> 10) & 0x1FF, 0x1FF);
    EXPECT_EQ((result >> 19) & 0x1FF, 0x1FF);
}

TEST_F(CcuAssistTest, CcuCombineTokenInfo_DelegatesToGetToken)
{
    uint64_t combined = ccu_combine_token_info(/*token_id=*/5, /*token_value=*/0xAB, /*token_valid=*/1);
    EXPECT_EQ(combined, get_token(5, 0xAB, 1));
}

TEST_F(CcuAssistTest, CcuCombineTokenInfo_ZeroValid)
{
    uint64_t combined = ccu_combine_token_info(3, 0x10, 0);
    EXPECT_EQ(combined, get_token(3, 0x10, 0));
}

} // namespace
} // namespace ccu_rep
} // namespace asc
