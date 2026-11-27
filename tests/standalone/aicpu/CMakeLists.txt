# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(asccomm_replay_tests LANGUAGES CXX)
enable_testing()
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
foreach(test IN ITEMS binary_template ub_queue_budget shared_tail binary_capture_oom replay_planning)
    add_executable(${test}_test ${test}_test.cc)
    target_compile_features(${test}_test PRIVATE cxx_std_17)
    target_compile_options(${test}_test PRIVATE -UNDEBUG)
    add_test(NAME ${test} COMMAND ${test}_test)
endforeach()
find_library(securec c_sec HINTS "$ENV{ASCEND_HOME_PATH}/lib64" REQUIRED)
set(replay_include_dirs "${root}/include/aicpu_ts/asccomm" "${root}/include/aicpu/asccomm"
    "${root}/pkg_inc/hcomm" "$ENV{ASCEND_HOME_PATH}/include")
foreach(test IN ITEMS binary_template binary_capture_oom replay_planning)
    target_include_directories(${test}_test PRIVATE ${replay_include_dirs})
    target_link_libraries(${test}_test PRIVATE ${securec})
endforeach()

add_library(binary_capture_core STATIC "${root}/src/aicpu/asccomm/primitives/task_cache/binary_capture.cc")
target_include_directories(binary_capture_core PRIVATE ${replay_include_dirs})
target_compile_features(binary_capture_core PRIVATE cxx_std_17)

option(ASCCOMM_BUILD_RUNTIME_UT "Build runtime integration tests with GTest" OFF)
if(ASCCOMM_BUILD_RUNTIME_UT)
    set(CMAKE_CXX_STANDARD 17)
    get_filename_component(ASCCOMM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    if(NOT ASCEND_CANN_PACKAGE_PATH)
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    endif()
    set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux")
    set(ASCCOMM_UT_V310_NPU_ARCH 3510 CACHE STRING "Target architecture for Host UT")
    find_package(GTest REQUIRED)
    add_library(intf_llt_pub_basic INTERFACE)
    target_link_libraries(intf_llt_pub_basic INTERFACE GTest::gtest)
    target_include_directories(intf_llt_pub_basic INTERFACE
        ${ASCEND_CANN_ARCH_DIR}/pkg_inc/driver)
    target_link_directories(intf_llt_pub_basic INTERFACE ${ASCEND_CANN_PACKAGE_PATH}/lib64)
    target_link_libraries(intf_llt_pub_basic INTERFACE c_sec unified_dlog)
    add_subdirectory(${ASCCOMM_ROOT}/tests/ut/aicpu/asccomm aicpu-tests)
endif()
