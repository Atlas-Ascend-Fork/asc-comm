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

set(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu/hcomm")
set(ASCCOMM_DFX_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/common/dfx")
set(HCOMM_SOURCE_DIR "" CACHE PATH "HCOMM source directory providing pkg_inc/")

if(NOT HCOMM_SOURCE_DIR)
    get_filename_component(_default_hcomm_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/../hcomm_pkg_inc" REALPATH)
    if(EXISTS "${_default_hcomm_source_dir}/pkg_inc/hcomm/hcomm_pod_resource.h")
        set(HCOMM_SOURCE_DIR "${_default_hcomm_source_dir}")
    endif()
endif()
if(NOT EXISTS "${HCOMM_SOURCE_DIR}/pkg_inc/hcomm/hcomm_primitives_types.h" OR
    NOT EXISTS "${HCOMM_SOURCE_DIR}/pkg_inc/hcomm/hcomm_pod_resource.h")
    message(FATAL_ERROR "Set HCOMM_SOURCE_DIR to the HCOMM source root")
endif()

include(cmake/fetch_cann_cmake.cmake)
project(asc_comm_data_plane LANGUAGES CXX)
init_cann_project(PREPEND_MODULE_PATH)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_cann_package(acl_rt MODULE REQUIRED)
find_cann_package(ascend_hal MODULE REQUIRED)
find_cann_package(securec MODULE REQUIRED)
find_cann_package(unified_dlog MODULE REQUIRED)

add_library(asc_comm_dfx INTERFACE)
target_include_directories(asc_comm_dfx INTERFACE ${ASCCOMM_DFX_SRC_DIR})

include(cmake/aicpu_asccomm.cmake)

add_library(asccomm_aicpu SHARED ${ASCCOMM_AICPU_SOURCES})

target_compile_definitions(asccomm_aicpu PRIVATE
    HCCD
    CCL_KERNEL_AICPU
    OPEN_BUILD_PROJECT
    _GLIBCXX_USE_CXX11_ABI=1
)

target_compile_options(asccomm_aicpu PRIVATE
    -Wfloat-equal
    -Wall
    -fno-common
    -fstack-protector-strong
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -pipe
    -O3
)

target_include_directories(asccomm_aicpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu
    ${CMAKE_CURRENT_SOURCE_DIR}/include/aicpu/hcomm
    ${HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_ROOT}/primitives/api_c_adpt
    ${ASCCOMM_ROOT}/primitives/launch
    ${ASCCOMM_ROOT}/common/compat/hcomm
    ${ASCCOMM_ROOT}/common/compat/hcomm/hccl
    ${ASCCOMM_ROOT}/common/exception
    ${ASCCOMM_ROOT}/common/utils
    ${ASCCOMM_ROOT}/resource/buffer
    ${ASCCOMM_ROOT}/resource/common
    ${ASCCOMM_ROOT}/resource/connection
    ${ASCCOMM_ROOT}/resource/stream
)

target_link_libraries(asccomm_aicpu PRIVATE
    asc_comm_dfx
    acl_rt_headers
    slog_headers
    ascend_hal
    c_sec
    dl
    pthread
)

target_link_options(asccomm_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/asccomm_exports.map
)

set_target_properties(asccomm_aicpu PROPERTIES SKIP_BUILD_RPATH TRUE)
