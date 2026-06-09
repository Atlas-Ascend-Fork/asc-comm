# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    set(ASCEND_CANN_PACKAGE_PATH "/home/developer/Ascend/cann-9.0.0" CACHE PATH "CANN package path")
endif()

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

set(ASCCOMM_CANN_INCLUDE_DIRS
    "${ASCEND_CANN_ARCH_DIR}/include"
    "${ASCEND_CANN_ARCH_DIR}/include/acl"
    "${ASCEND_CANN_ARCH_DIR}/include/hccl"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc"
    "${ASCEND_CANN_ARCH_DIR}/pkg_inc/base"
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
    error_manager_headers
    c_sec_headers
    slog_headers
    msprof_headers
    npu_runtime_headers
    mmpa_headers
    ofed_headers
)
    asccomm_add_header_target(${target_name})
endforeach()

function(asccomm_add_imported_library target_name library_name)
    if(TARGET ${target_name})
        return()
    endif()

    find_library(ASCCOMM_${target_name}_LIB
        NAMES ${library_name}
        PATHS
            "${ASCEND_CANN_ARCH_DIR}/lib64"
            "${ASCEND_CANN_ARCH_DIR}/lib64/stub"
            "${ASCEND_CANN_ARCH_DIR}/lib64/stub/aicpu_kernels"
        NO_DEFAULT_PATH
    )

    if(ASCCOMM_${target_name}_LIB)
        add_library(${target_name} UNKNOWN IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES IMPORTED_LOCATION "${ASCCOMM_${target_name}_LIB}")
        target_include_directories(${target_name} INTERFACE ${ASCCOMM_CANN_INCLUDE_DIRS})
    else()
        add_library(${target_name} INTERFACE)
        target_include_directories(${target_name} INTERFACE ${ASCCOMM_CANN_INCLUDE_DIRS})
    endif()
endfunction()

asccomm_add_imported_library(acl_rt ascendcl)
asccomm_add_imported_library(c_sec c_sec)
asccomm_add_imported_library(unified_dlog slog)
asccomm_add_imported_library(mmpa mmpa)

if(NOT TARGET hcomm)
    add_library(hcomm INTERFACE)
    target_include_directories(hcomm INTERFACE ${ASCCOMM_CANN_INCLUDE_DIRS})
endif()
