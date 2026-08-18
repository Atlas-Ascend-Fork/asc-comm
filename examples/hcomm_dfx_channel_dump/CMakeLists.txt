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

project(hcomm_dfx_channel_dump LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(HCOMM_STAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package_layout")
set(HCOMM_STAGE_INCLUDE_DIR "${HCOMM_STAGE_ROOT}/asc/include/adv_api")
set(HCOMM_STAGE_DETAIL_DIR "${HCOMM_STAGE_ROOT}/asc/impl/adv_api/detail/hcomm")

add_custom_target(prepare_hcomm_header_layout
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${HCOMM_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${HCOMM_STAGE_DETAIL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HCOMM_STAGE_INCLUDE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HCOMM_STAGE_DETAIL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/hcomm"
        "${HCOMM_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/hcomm/detail"
        "${HCOMM_STAGE_DETAIL_DIR}"
    COMMENT "Preparing Hcomm headers in the installed package layout"
)

set_source_files_properties(
    hcomm_dfx_channel_dump_demo.asc
    sample_allgather_kernel.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/detail/dfx/hcomm_dfx_queue_reader.cpp
    ../hcomm_write_read_nbi/utils.cpp
    PROPERTIES LANGUAGE ASC
)

add_executable(hcomm_dfx_channel_dump_demo
    hcomm_dfx_channel_dump_demo.asc
    sample_allgather_kernel.cpp
    sample_allgather_def.h
    ${ASCCOMM_ROOT}/src/aicore/hcomm/detail/dfx/hcomm_channel_dump.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/detail/dfx/hcomm_dfx_queue_reader.cpp
    ../hcomm_write_read_nbi/utils.cpp
)
add_dependencies(hcomm_dfx_channel_dump_demo
    prepare_hcomm_header_layout
)

target_compile_definitions(hcomm_dfx_channel_dump_demo PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
)
target_include_directories(hcomm_dfx_channel_dump_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../hcomm_write_read_nbi
    ${ASCCOMM_ROOT}/src/aicore/hcomm/detail/dfx
    ${HCOMM_STAGE_ROOT}/asc
    ${HCOMM_STAGE_ROOT}/asc/include
    ${HCOMM_STAGE_INCLUDE_DIR}
    $ENV{ASCEND_HOME_PATH}/asc/include
    $ENV{ASCEND_HOME_PATH}/asc/include/basic_api
    $ENV{ASCEND_HOME_PATH}/include
)
target_compile_options(hcomm_dfx_channel_dump_demo PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)
target_link_directories(hcomm_dfx_channel_dump_demo PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    /usr/local/Ascend/driver/lib64/driver
    $ENV{ASCEND_HOME_PATH}/devlib
)
target_link_libraries(hcomm_dfx_channel_dump_demo PRIVATE
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
    hccl
    hcomm
    c_sec
    ascend_hal
)
set_target_properties(hcomm_dfx_channel_dump_demo PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
