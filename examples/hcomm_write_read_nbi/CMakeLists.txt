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
    message(FATAL_ERROR "hcomm_write_read_nbi only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(kernel_samples LANGUAGES ASC CXX)

get_filename_component(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(ASCCOMM_CANN_ARCH "x86_64-linux")
endif()

set(ASCEND_CANN_ARCH_DIR "$ENV{ASCEND_HOME_PATH}/${ASCCOMM_CANN_ARCH}")
if(NOT EXISTS "${ASCEND_CANN_ARCH_DIR}")
    set(ASCEND_CANN_ARCH_DIR "$ENV{ASCEND_HOME_PATH}/aarch64-linux")
endif()

set_source_files_properties(
    hcomm_write_read_nbi_kernel.cpp
    utils.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx_queue_reader.cpp
    hcomm_rw_def.h
    PROPERTIES LANGUAGE ASC
)

add_executable(demo
    hcomm_write_read_nbi.asc
    hcomm_write_read_nbi_kernel.cpp
    hcomm_rw_def.h
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx_exception.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx_collect.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx_parse.cpp
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host/hcomm_dfx_queue_reader.cpp
    utils.cpp
)

target_include_directories(demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/common
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/impl
    ${ASCCOMM_ROOT}/include/aicore
    $ENV{ASCEND_HOME_PATH}/asc/include
    $ENV{ASCEND_HOME_PATH}/asc/include/basic_api
    ${ASCEND_CANN_ARCH_DIR}/asc/impl/basic_api
    $ENV{ASCEND_HOME_PATH}/include
)

# 链接 hcomm 库：Host 侧通信域创建(HcclCommInitRootInfo)、内存注册(HcclCommMemReg)、
# 通道创建(HcclChannelAcquire)等接口由 libhcomm.so 提供。
target_link_directories(demo PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    /usr/local/Ascend/driver/lib64/driver
    $ENV{ASCEND_HOME_PATH}/devlib
)
target_link_libraries(demo PRIVATE
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
set_target_properties(demo PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

target_compile_options(demo PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)
