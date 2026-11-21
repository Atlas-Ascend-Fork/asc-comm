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
project(simt_jetty LANGUAGES ASC CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(HCOMM_CCE_RES_USAGE "Print CCE stack and register usage" OFF)
set(SIMT_JETTY_SGE_NUM "12" CACHE STRING "Compile-time SGE count for warp/group/dispatch/mixed modes (1 to 12)")
if(NOT SIMT_JETTY_SGE_NUM MATCHES "^[1-9]$|^1[0-2]$")
    message(FATAL_ERROR "SIMT_JETTY_SGE_NUM must be an integer in [1, 12]")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_source_files_properties(op_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(simt_jetty
    simt_jetty.asc
    op_kernel.cpp
)

target_compile_definitions(simt_jetty PRIVATE
    USE_SIMT
    SIMT_JETTY_SGE_NUM=${SIMT_JETTY_SGE_NUM}
)

set_target_properties(simt_jetty PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(simt_jetty PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    $ENV{ASCEND_HOME_PATH}/include
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/adv_api>
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/comm_api/aicore>
)

target_link_directories(simt_jetty PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(simt_jetty PRIVATE
    hcomm
    hccl
    ascendcl
    acl_rt
    runtime
    c_sec
    dl
    pthread
)

target_compile_options(simt_jetty PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
    $<$<AND:$<COMPILE_LANGUAGE:ASC>,$<BOOL:${HCOMM_CCE_RES_USAGE}>>:--cce-res-usage>
)
