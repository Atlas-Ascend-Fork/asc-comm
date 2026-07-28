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

set(ASC_COMM_AICPU_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu/hcomm")
set(ASC_COMM_DFX_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/common/dfx")
set(ASC_COMM_AICPU_INCLUDE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu")
set(ASC_COMM_AICPU_INCLUDE_DIR "${ASC_COMM_AICPU_INCLUDE_ROOT_DIR}/hcomm")

include(cmake/fetch_cann_cmake.cmake)
project(asc_comm_aicpu_data_plane LANGUAGES CXX)
init_cann_project(PREPEND_MODULE_PATH)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_cann_package(acl_rt MODULE REQUIRED)
find_cann_package(ascend_hal MODULE REQUIRED)
find_cann_package(securec MODULE REQUIRED)
find_cann_package(unified_dlog MODULE REQUIRED)

add_library(asc_comm_dfx INTERFACE)
target_include_directories(asc_comm_dfx INTERFACE ${ASC_COMM_DFX_SRC_DIR})

include(cmake/aicpu_data_plane_sources.cmake)

# The source list is the A5 AICPU data-plane build closure only.

set(ASC_COMM_AICPU_INCLUDE_LIST
    ${ASC_COMM_AICPU_INCLUDE_ROOT_DIR}
    ${ASC_COMM_AICPU_INCLUDE_DIR}
    ${ASC_COMM_AICPU_SRC_DIR}/common/compat
    ${ASC_COMM_AICPU_SRC_DIR}/common/compat/hcomm
    ${ASC_COMM_AICPU_SRC_DIR}/common/compat/hcomm/hccl
    ${ASC_COMM_AICPU_SRC_DIR}/common/compat/hccl/framework/inc
    ${ASC_COMM_AICPU_SRC_DIR}/common/compat/hccl/pub_inc
    ${ASC_COMM_AICPU_SRC_DIR}/common/exception
    ${ASC_COMM_AICPU_SRC_DIR}/common/platform/ascend950
    ${ASC_COMM_AICPU_SRC_DIR}/common/types
    ${ASC_COMM_AICPU_SRC_DIR}/common/utils
    ${ASC_COMM_AICPU_SRC_DIR}/primitives/api_c_adpt
    ${ASC_COMM_AICPU_SRC_DIR}/primitives/launch
    ${ASC_COMM_AICPU_SRC_DIR}/resource/buffer
    ${ASC_COMM_AICPU_SRC_DIR}/resource/channel
    ${ASC_COMM_AICPU_SRC_DIR}/resource/common
    ${ASC_COMM_AICPU_SRC_DIR}/resource/communicator
    ${ASC_COMM_AICPU_SRC_DIR}/resource/connection
    ${ASC_COMM_AICPU_SRC_DIR}/resource/notify
    ${ASC_COMM_AICPU_SRC_DIR}/resource/stream
    ${ASC_COMM_AICPU_SRC_DIR}/resource/thread
    ${ASC_COMM_AICPU_SRC_DIR}/resource/transport/common
    ${ASC_COMM_AICPU_SRC_DIR}/resource/transport/p2p
    ${ASC_COMM_AICPU_SRC_DIR}/resource/transport/roce
    ${ASC_COMM_AICPU_SRC_DIR}/resource/transport/ubc
)

set(ASC_COMM_AICPU_COMPILE_DEFINITIONS
    HCCD
    CCL_KERNEL_AICPU
    OPEN_BUILD_PROJECT
    _GLIBCXX_USE_CXX11_ABI=1
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
)

add_library(aicpu_data_plane SHARED ${ASC_COMM_AICPU_DATA_PLANE_SOURCES})
target_compile_definitions(aicpu_data_plane PRIVATE ${ASC_COMM_AICPU_COMPILE_DEFINITIONS})
target_compile_options(aicpu_data_plane PRIVATE ${ASC_COMM_AICPU_COMPILE_OPTIONS})
target_link_options(aicpu_data_plane PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -s
)
target_include_directories(aicpu_data_plane PRIVATE ${ASC_COMM_AICPU_INCLUDE_LIST})
target_link_libraries(aicpu_data_plane PRIVATE
    asc_comm_dfx
    acl_rt_headers
    slog_headers
    ascend_hal
    c_sec
    dl
    pthread
)
set_target_properties(aicpu_data_plane PROPERTIES SKIP_BUILD_RPATH TRUE)
