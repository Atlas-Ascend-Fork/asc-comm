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

set(ASC_COMM_AICPU_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu")
set(ASC_COMM_AICPU_INCLUDE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu")
set(ASC_COMM_AICPU_INCLUDE_DIR "${ASC_COMM_AICPU_INCLUDE_ROOT_DIR}/hcomm")

include(cmake/fetch_cann_cmake.cmake)
project(asc_comm_aicpu_data_plane LANGUAGES CXX)
init_cann_project(PREPEND_MODULE_PATH)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_cann_package(ascend_hal MODULE REQUIRED)
find_cann_package(securec MODULE REQUIRED)
find_cann_package(unified_dlog MODULE REQUIRED)

include(cmake/aicpu_data_plane_sources.cmake)

# The source list is the A5 AICPU data-plane build closure only.

set(ASC_COMM_AICPU_INCLUDE_LIST
    ${ASC_COMM_AICPU_INCLUDE_ROOT_DIR}
    ${ASC_COMM_AICPU_INCLUDE_DIR}
    ${ASC_COMM_AICPU_SRC_DIR}/common/hcomm_compat
    ${ASC_COMM_AICPU_SRC_DIR}/common/hcomm_compat/hccl
)
foreach(include_root
    ${ASC_COMM_AICPU_SRC_DIR}/base_comm
    ${ASC_COMM_AICPU_SRC_DIR}/coll_communicator_mgr
    # Header-only HCCL compatibility dependency kept by the A5 AICPU data-plane
    # closure. No non-A5 source files are built into libaicpu_data_plane.so.
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
    slog_headers
    ascend_hal
    c_sec
    dl
    pthread
)
set_target_properties(aicpu_data_plane PROPERTIES SKIP_BUILD_RPATH TRUE)
