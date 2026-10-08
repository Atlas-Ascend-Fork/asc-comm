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
find_package(ASC REQUIRED)
project(multi_jetty_batch_perf LANGUAGES ASC CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(MULTI_JETTY_MATRIX_BATCH "256" CACHE STRING "One batch size: 1,8,32,64,128,256")
if(NOT MULTI_JETTY_MATRIX_BATCH MATCHES "^(1|8|32|64|128|256)$")
    message(FATAL_ERROR "Select one batch size: 1,8,32,64,128,256")
endif()
set_source_files_properties(simt_kernel.cpp PROPERTIES LANGUAGE ASC)
add_executable(multi_jetty_batch_perf main.cpp simt_kernel.cpp)
set_target_properties(multi_jetty_batch_perf PROPERTIES LINKER_LANGUAGE ASC)
target_compile_definitions(multi_jetty_batch_perf PRIVATE MULTI_JETTY_MATRIX_BATCH=${MULTI_JETTY_MATRIX_BATCH})
set_property(SOURCE simt_kernel.cpp APPEND PROPERTY COMPILE_DEFINITIONS USE_SIMT)
# Use the Hcomm headers installed by the asc-comm run package.
target_include_directories(multi_jetty_batch_perf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    $ENV{ASCEND_HOME_PATH}/include
    $ENV{ASCEND_HOME_PATH}/asc/include/adv_api)
target_link_directories(multi_jetty_batch_perf PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)
target_link_libraries(multi_jetty_batch_perf PRIVATE hcomm hccl ascendcl acl_rt runtime c_sec dl pthread)
target_compile_options(multi_jetty_batch_perf PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-res-usage>
)
