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

include(version.cmake)
if(NOT CANN_3RD_LIB_PATH)
    set(CANN_3RD_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "CANN third_party path")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch_cann_cmake.cmake")
project(asc_comm VERSION "${ASCCOMM_VERSION}" LANGUAGES C CXX)
init_cann_project(PREPEND_MODULE_PATH)
add_cann_target_options()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build asc-comm CCU library" ON)
option(ASCCOMM_BUILD_AICPU "Build asc-comm AICPU data plane library" ON)
option(ASCCOMM_BUILD_CPU_TS "Build asc-comm CPU TS synchronization library" ON)

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

if(ASCCOMM_BUILD_AICPU)
    include(ExternalProject)
    ExternalProject_Add(asccomm_aicpu_device
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/device"
        BINARY_DIR "${CMAKE_BINARY_DIR}/package-aicpu-device"
        CMAKE_ARGS
            "-DASCEND_CANN_PACKAGE_PATH=${ASCEND_CANN_PACKAGE_PATH}"
            "-DCANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH}"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "-DTOOLCHAIN_DIR=${ASCEND_CANN_PACKAGE_PATH}/toolkit/toolchain/hcc"
            "-DCMAKE_TOOLCHAIN_FILE=${CANN_CMAKE_DIR}/toolchain/aarch64-hcc-toolchain.cmake"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build . --target pack_asccomm_aicpu
        INSTALL_COMMAND ""
    )
endif()

if(ASCCOMM_BUILD_CPU_TS)
    add_subdirectory(src/cpu)
endif()
