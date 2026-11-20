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
// Original source: opbase_test/hccl_allgather_test.h

#ifndef HCCL_API_TEST_ALLGATHER_TEST_H
#define HCCL_API_TEST_ALLGATHER_TEST_H

#include "hccl_api_test_common.h"

namespace hccl_api_test {

class HcclOpBaseAllgatherTest : public HcclOpBaseTest {
public:
    const char* op_name() const override { return "AllGather"; }
    int hccl_op_base_test() override;

protected:
    size_t init_malloc_Ksize_by_data() override;
    void init_send_recv_size_by_data(size_t& send_bytes, size_t& recv_bytes) override;
    bool init_case_shape() override;
    int init_op_executor(CaseResources& res) override;
    int execute_op(CaseResources& res, aclrtStream stream) override;
    std::vector<uint8_t> build_expected() const override;
    bool reports_kernel_wait_time() const override;
};

} // namespace hccl_api_test

#endif
