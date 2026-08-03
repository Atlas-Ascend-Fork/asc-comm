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

find_package(ASC REQUIRED)

project(hcomm_write_nbi LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(demo
    hcomm_write_nbi.asc
    hcomm_write_nbi_control_plane.cpp
)

get_filename_component(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_include_directories(demo PRIVATE
    ${ASCCOMM_ROOT}/include/aicore
    ${ASCCOMM_ROOT}/src/aicore
    $ENV{ASCEND_HOME_PATH}/include
)

target_link_directories(demo PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
)

target_compile_options(demo PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

target_link_libraries(demo PRIVATE
    -Wl,--no-as-needed
    ascendc_runtime
    acl_rt
    runtime
    register
    error_manager
    profapi
    ge_common_base
    unified_dlog
    mmpa
    dl
    ascend_dump
    c_sec
    hccl
    hcomm
    pthread
    -Wl,--as-needed
)
