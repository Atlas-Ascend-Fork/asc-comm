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
project(batch_perf_compare LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_source_files_properties(simd_kernel.cpp simt_multi_jetty_kernel.cpp PROPERTIES LANGUAGE ASC)
set_property(SOURCE simt_multi_jetty_kernel.cpp APPEND PROPERTY COMPILE_DEFINITIONS USE_SIMT)

get_filename_component(ASCCOMM_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(HCOMM_SOURCE_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/source_overlay/asc")
file(MAKE_DIRECTORY
    "${HCOMM_SOURCE_OVERLAY}/include/adv_api/hcomm"
    "${HCOMM_SOURCE_OVERLAY}/impl/comm_api/aicore/hcomm/host"
)
configure_file(
    "${ASCCOMM_SOURCE_ROOT}/include/aicore/hcomm/hcomm_host.h"
    "${HCOMM_SOURCE_OVERLAY}/include/adv_api/hcomm/hcomm_host.h"
    COPYONLY
)
configure_file(
    "${ASCCOMM_SOURCE_ROOT}/src/aicore/hcomm/host/hcomm_host_impl.h"
    "${HCOMM_SOURCE_OVERLAY}/impl/comm_api/aicore/hcomm/host/hcomm_host_impl.h"
    COPYONLY
)

add_executable(batch_perf_compare main.cpp simd_kernel.cpp simt_multi_jetty_kernel.cpp)
set_target_properties(batch_perf_compare PROPERTIES LINKER_LANGUAGE ASC)
target_include_directories(batch_perf_compare BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HCOMM_SOURCE_OVERLAY}/include/adv_api
    $ENV{ASCEND_HOME_PATH}/include
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/adv_api>
)
target_link_directories(batch_perf_compare PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)
target_link_libraries(batch_perf_compare PRIVATE hcomm hccl ascendcl acl_rt runtime c_sec dl pthread)
target_compile_options(batch_perf_compare PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)
