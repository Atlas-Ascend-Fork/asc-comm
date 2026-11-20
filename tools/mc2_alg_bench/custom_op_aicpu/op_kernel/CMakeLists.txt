# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

configure_file(hccl_api_test_measurement.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/hccl_api_test_measurement.h @ONLY)
set(HCCL_API_TEST_KERNEL_OPTIONS -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_CURRENT_BINARY_DIR})

# set custom compile options
if ("${CMAKE_BUILD_TYPE}x" STREQUAL "Debugx")
    list(APPEND HCCL_API_TEST_KERNEL_OPTIONS -g -O0)
endif()
add_ops_compile_options(ALL OPTIONS ${HCCL_API_TEST_KERNEL_OPTIONS})

add_kernels_compile()
