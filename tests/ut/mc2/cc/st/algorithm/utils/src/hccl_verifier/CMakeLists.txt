# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
add_library(${HCCL_CHECKER_VERIFIER_TARGET} STATIC "")
target_sources(${HCCL_CHECKER_VERIFIER_TARGET} PRIVATE
    checker.cc
    hccl_verifier.cc
)

add_subdirectory(mem_conflict_check)
add_subdirectory(semantics_check)
add_subdirectory(singletask_check)
add_subdirectory(task_graph_generator)

target_compile_options(${HCCL_CHECKER_VERIFIER_TARGET} PRIVATE
    -std=c++14
    -Werror
    -fno-common
    -fno-strict-aliasing
    -pipe
    -O0
    -g
    $<$<BOOL:${ASCCOMM_GTEST_USES_CXX11_ABI0}>:-D_GLIBCXX_USE_CXX11_ABI=0>
)

message(STATUS "utils path= ${UTILS_DIR}")

target_compile_definitions(${HCCL_CHECKER_VERIFIER_TARGET} PRIVATE
    -DUT_TEST
    -DAICPU_COMPILE
)

target_include_directories(${HCCL_CHECKER_VERIFIER_TARGET} PUBLIC
    ${HCCL_CHECKER_COMMON_INCLUDE_LIST}
    ${UTILS_DIR}/pub_inc
)
