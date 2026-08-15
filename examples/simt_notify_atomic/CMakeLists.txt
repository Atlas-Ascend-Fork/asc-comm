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

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture: dav-3510")

if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "simt_notify_atomic only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(simt_notify_atomic LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Only the kernel is compiled as ASC. main.cpp remains on the host C++ toolchain.
set_source_files_properties(
    op_kernel.cpp
    PROPERTIES LANGUAGE ASC
)

add_executable(simt_notify_atomic
    main.cpp
    op_kernel.cpp
)

target_compile_definitions(simt_notify_atomic PRIVATE
    USE_SIMT
)

set_target_properties(simt_notify_atomic PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(simt_notify_atomic BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{ASCEND_HOME_PATH}/include
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/adv_api>
)

# Host-side communication-domain, memory-registration and channel APIs are provided by libhcomm.so.
target_link_directories(simt_notify_atomic PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(simt_notify_atomic PRIVATE
    hcomm
    hccl
    ascendcl
    acl_rt
    runtime
    c_sec
    dl
    pthread
)

target_compile_options(simt_notify_atomic PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)
