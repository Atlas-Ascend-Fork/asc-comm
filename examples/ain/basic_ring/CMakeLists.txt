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

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture")

if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "ain_basic_ring_sample only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(ain_basic_ring_sample LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_source_files_properties(
    basic_ring.asc
    PROPERTIES LANGUAGE ASC
)

add_executable(basic_ring_demo
    basic_ring.asc
)

target_include_directories(basic_ring_demo PRIVATE
    ${ASCEND_CANN_PACKAGE_LINUX_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_LINUX_PATH}/include
    ${ASCEND_CANN_PACKAGE_LINUX_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_LINUX_PATH}/asc/include/comm_api/aicore
)

target_link_directories(basic_ring_demo PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
    ${ASCEND_CANN_PACKAGE_LINUX_PATH}/lib64
)

target_link_libraries(basic_ring_demo PRIVATE
    -Wl,--no-as-needed
    hccl
    hcomm
    -Wl,--as-needed
    ascendcl
    runtime
    stdc++
    dl
)

set_target_properties(basic_ring_demo PROPERTIES
    BUILD_RPATH "${ASCEND_CANN_PACKAGE_PATH}/lib64;${ASCEND_CANN_PACKAGE_LINUX_PATH}/lib64"
)
