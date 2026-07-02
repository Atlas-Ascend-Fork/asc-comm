# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)

option(BUILD_OPEN_PROJECT "Build open asc-comm project." ON)
option(ENABLE_AICPU_DATA_PLANE "Build A5 AICPU data-plane shared objects." ON)

if(NOT DEFINED PRODUCT_SIDE)
    set(PRODUCT_SIDE "device")
endif()

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    else()
        set(ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/ascend-toolkit/latest")
    endif()
endif()

set(HCOMM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ASC_COMM_AICPU_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu")
set(ASC_COMM_AICPU_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu")
set(HCC_TOOLCHAIN_DIR "${ASCEND_CANN_PACKAGE_PATH}/toolkit/toolchain/hcc")
set(HCC_CXX_COMPILER "${HCC_TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-g++")
set(HCC_C_COMPILER "${HCC_TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-gcc")
set(HCC_C_AR "${HCC_TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ar")

include(cmake/fetch_cann_cmake.cmake)
project(asc_comm_aicpu_data_plane)
init_cann_project(PREPEND_MODULE_PATH)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")

include(cmake/config.cmake)
include(cmake/hcomm_utils.cmake)
include(cmake/third_party/ubengine.cmake)

add_cann_third_party(json)
add_cann_third_party(openssl)
add_cann_third_party(rdma-core)

find_cann_package(acl_rt MODULE REQUIRED)
find_cann_package(aicpu_sharder MODULE REQUIRED)
find_cann_package(asc_devkit MODULE REQUIRED)
find_cann_package(ascend_hal MODULE REQUIRED)
find_cann_package(atrace MODULE REQUIRED)
find_cann_package(error_manager MODULE REQUIRED)
find_cann_package(mmpa MODULE REQUIRED)
find_cann_package(msprof MODULE REQUIRED)
find_cann_package(runtime MODULE REQUIRED)
find_cann_package(securec MODULE REQUIRED)
find_cann_package(tsdclient MODULE REQUIRED)
find_cann_package(unified_dlog MODULE REQUIRED)

include(cmake/aicpu_data_plane_sources.cmake)

# The source list is the A5 AICPU data-plane build closure only.

set(ASC_COMM_AICPU_INCLUDE_LIST
    ${ASC_COMM_AICPU_INCLUDE_DIR}
    ${ASC_COMM_AICPU_INCLUDE_DIR}/hccl
    ${ASC_COMM_AICPU_INCLUDE_DIR}/tsch
)
foreach(include_root
    ${ASC_COMM_AICPU_SRC_DIR}/base_comm
    ${ASC_COMM_AICPU_SRC_DIR}/coll_communicator_mgr
    # Header-only HCCL compatibility dependency kept by the A5 AICPU data-plane
    # closure. No non-A5 source files are built into libccl_kernel.so.
    ${ASC_COMM_AICPU_SRC_DIR}/common/hccl_compat
    ${ASC_COMM_AICPU_SRC_DIR}/legacy/ascend950
)
    if(EXISTS "${include_root}")
        file(GLOB_RECURSE include_dirs FOLLOW_SYMLINKS LIST_DIRECTORIES TRUE "${include_root}/*")
        list(APPEND ASC_COMM_AICPU_INCLUDE_LIST ${include_root})
        foreach(dir ${include_dirs})
            if(IS_DIRECTORY "${dir}")
                list(APPEND ASC_COMM_AICPU_INCLUDE_LIST "${dir}")
            endif()
        endforeach()
    endif()
endforeach()
list(REMOVE_DUPLICATES ASC_COMM_AICPU_INCLUDE_LIST)

set(ASC_COMM_AICPU_COMPILE_DEFINITIONS
    HCCD
    CCL_KERNEL_AICPU
    OPEN_BUILD_PROJECT
    -D_GLIBCXX_USE_CXX11_ABI=1
)

set(ASC_COMM_AICPU_COMPILE_OPTIONS
    -Werror
    -Wfloat-equal
    -Wall
    -fno-common
    -fstack-protector-strong
    -fno-strict-aliasing
    -pipe
    -O3
    -std=c++17
)

add_library(ccl_kernel SHARED ${ASC_COMM_CCL_KERNEL_SOURCES})
target_compile_definitions(ccl_kernel PRIVATE ${ASC_COMM_AICPU_COMPILE_DEFINITIONS})
target_compile_options(ccl_kernel PRIVATE ${ASC_COMM_AICPU_COMPILE_OPTIONS})
target_link_options(ccl_kernel PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -s
)
target_include_directories(ccl_kernel PRIVATE ${ASC_COMM_AICPU_INCLUDE_LIST})
target_link_libraries(ccl_kernel PRIVATE
    $<BUILD_INTERFACE:acl_rt_headers>
    $<BUILD_INTERFACE:asc_host_headers>
    $<BUILD_INTERFACE:asc_kernel_headers>
    $<BUILD_INTERFACE:ascend_hal_headers>
    $<BUILD_INTERFACE:kernel_tiling_headers>
    $<BUILD_INTERFACE:slog_headers>
    $<BUILD_INTERFACE:atrace_headers>
    $<BUILD_INTERFACE:mmpa_headers>
    $<BUILD_INTERFACE:runtime_headers>
    $<BUILD_INTERFACE:rdma_core_headers>
    $<BUILD_INTERFACE:json>
    -Wl,--no-as-needed
    ascend_hal
    c_sec
    mmpa
    -Wl,--as-needed
    -lrt
    -ldl
    -lpthread
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ccl_kernel.json
    COMMAND ${HI_PYTHON} ${HCOMM_DIR}/cmake/scripts/parser_ini.py
                         ${ASC_COMM_AICPU_SRC_DIR}/legacy/ascend950/framework/communicator/aicpu/ccl_kernel.ini
                         ${CMAKE_CURRENT_BINARY_DIR}/ccl_kernel.json
    WORKING_DIRECTORY ${HCOMM_DIR}
    COMMENT "Generating ccl_kernel.json"
    VERBATIM
)
add_custom_target(ccl_kernel_json ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ccl_kernel.json
)

install(TARGETS ccl_kernel
    LIBRARY DESTINATION ${INSTALL_DEVICE_LIBRARY_DIR} ${INSTALL_OPTIONAL}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ccl_kernel.json
    DESTINATION ${INSTALL_CCL_KERNEL_JSON_DIR}/config ${INSTALL_OPTIONAL}
)
