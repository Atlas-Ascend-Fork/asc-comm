# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

if(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_ASCEND_CANN_PACKAGE_PATH "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "")
elseif(DEFINED ASCEND_CANN_PACKAGE_PATH)
    set(_ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "")
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(_ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
elseif(IS_DIRECTORY "$ENV{HOME}/Ascend/cann")
    set(_ASCEND_CANN_PACKAGE_PATH "$ENV{HOME}/Ascend/cann" CACHE PATH "")
else()
    set(_ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/cann" CACHE PATH "")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "CANN package path")

if(NOT EXISTS "${ASCEND_CANN_PACKAGE_PATH}")
    message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH does not exist: ${ASCEND_CANN_PACKAGE_PATH}")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(ASCCOMM_CANN_ARCH "x86_64-linux")
endif()

set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${ASCCOMM_CANN_ARCH}")
if(NOT EXISTS "${ASCEND_CANN_ARCH_DIR}")
    set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux")
endif()

set(ASCENDC_DIR "${ASCEND_CANN_ARCH_DIR}/asc" CACHE PATH "AscendC headers path")

list(PREPEND CMAKE_PREFIX_PATH "${ASCEND_CANN_PACKAGE_PATH}")

if(NOT COMMAND find_cann_package)
    message(FATAL_ERROR "find_cann_package is unavailable. Please provide cann-cmake through CANN_3RD_LIB_PATH or network fetch.")
endif()

find_cann_package(acl_rt MODULE REQUIRED)
find_cann_package(aicpu_sharder MODULE REQUIRED)
find_cann_package(ascend_hal MODULE REQUIRED)
find_cann_package(atrace MODULE REQUIRED)
find_cann_package(securec MODULE REQUIRED)
find_cann_package(unified_dlog MODULE REQUIRED)
find_cann_package(mmpa MODULE REQUIRED)
find_cann_package(error_manager MODULE REQUIRED)
find_cann_package(metadef MODULE REQUIRED)
find_cann_package(runtime MODULE REQUIRED)
find_cann_package(msprof MODULE REQUIRED)
find_cann_package(tsdclient MODULE REQUIRED)

set(ASCCOMM_CANN_INCLUDE_DIRS
    "${ASCEND_CANN_ARCH_DIR}/include"
    "${ASCEND_CANN_ARCH_DIR}/include/acl"
    "${ASCEND_CANN_ARCH_DIR}/include/hccl"
    "${ASCEND_CANN_ARCH_DIR}/include/hcomm"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/base"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/hccl"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/hcomm/ccu"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/mmpa"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/profiling"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime/runtime"
    "${ASCENDC_DIR}"
    "${ASCENDC_DIR}/include"
    "${ASCENDC_DIR}/include/basic_api"
    "${ASCENDC_DIR}/include/simt_api"
    "${ASCENDC_DIR}/include/adv_api"
    "${ASCENDC_DIR}/include/utils"
    "${ASCENDC_DIR}/include/utils/tiling"
    "${ASCENDC_DIR}/impl"
    "${ASCENDC_DIR}/impl/basic_api"
    "${ASCENDC_DIR}/impl/simt_api"
)

function(asccomm_add_header_target target_name)
    if(NOT TARGET ${target_name})
        add_library(${target_name} INTERFACE)
        target_include_directories(${target_name} INTERFACE ${ASCCOMM_CANN_INCLUDE_DIRS})
    endif()
endfunction()

foreach(target_name IN ITEMS
    runtime_headers
    acl_rt_headers
    ascend_hal_headers
    atrace_headers
    error_manager_headers
    c_sec_headers
    kernel_tiling_headers
    metadef_headers
    slog_headers
    msprof_headers
    npu_runtime_headers
    mmpa_headers
    ofed_headers
)
    asccomm_add_header_target(${target_name})
endforeach()

if(NOT TARGET hcomm)
    add_library(hcomm INTERFACE)
    target_include_directories(hcomm INTERFACE ${ASCCOMM_CANN_INCLUDE_DIRS})
endif()
