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

project(asc_comm LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build asc-comm CCU launch wrapper library" ON)

if(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}")
elseif(ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
else()
    set(_ASCCOMM_CANN_PACKAGE_PATH "/usr/local/Ascend/cann")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_ASCCOMM_CANN_PACKAGE_PATH}" CACHE PATH "CANN package path")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(_ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(_ASCCOMM_CANN_ARCH "x86_64-linux")
endif()
set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}" CACHE PATH
    "Architecture-specific CANN package path")
if(ASCEND_CANN_PACKAGE_PATH AND NOT EXISTS "${ASCCOMM_CANN_ARCH_DIR}")
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux" CACHE PATH
        "Architecture-specific CANN package path" FORCE)
endif()

if(ASCCOMM_BUILD_CCU)
    add_subdirectory(src/ccu)
endif()

option(ASCCOMM_BUILD_AICPU_DATA_PLANE "Build AICPU data plane shared library" OFF)

if(ASCCOMM_BUILD_AICPU_DATA_PLANE)
    set(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/aicpu/hcomm")
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
    init_cann_project(PREPEND_MODULE_PATH)

    find_cann_package(acl_rt MODULE REQUIRED)
    find_cann_package(ascend_hal MODULE REQUIRED)
    find_cann_package(securec MODULE REQUIRED)
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
        ${HCOMM_SOURCE_DIR}/pkg_inc/hcomm
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
        acl_rt_headers
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
endif()
