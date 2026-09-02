# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_DFX_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_DFX_ROOT}/../../../.. ABSOLUTE)

option(ASCCOMM_BUILD_HCOMM_DFX "Build asc-comm HCOMM DFX helper library" ON)
if(NOT ASCCOMM_BUILD_HCOMM_DFX)
    return()
endif()

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture: dav-3510")
if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "asccomm_hcomm_dfx only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

if(NOT ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
    endif()
endif()

if(NOT ASCCOMM_CANN_ARCH_DIR OR NOT EXISTS "${ASCCOMM_CANN_ARCH_DIR}")
    foreach(_candidate
        "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux"
        "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux"
        "${ASCEND_CANN_PACKAGE_PATH}/x86_64-linux"
        "${ASCEND_CANN_PACKAGE_PATH}")
        if(EXISTS "${_candidate}")
            set(ASCCOMM_CANN_ARCH_DIR "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT ASCCOMM_CANN_ARCH_DIR)
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux")
endif()

find_package(ASC REQUIRED)
enable_language(ASC)

set(ASCCOMM_DFX_PUBLIC_INCLUDE_DIRS
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/aicore
)

set(ASCCOMM_DFX_PRIVATE_INCLUDE_DIRS
    ${ASCCOMM_ROOT}/src
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/common
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/host
    ${ASCCOMM_ROOT}/src/aicore/hcomm/dfx/impl
)

set(ASCCOMM_DFX_CANN_INCLUDE_DIRS
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/basic_api
    ${ASCCOMM_CANN_ARCH_DIR}/asc
    ${ASCCOMM_CANN_ARCH_DIR}/asc/include
    ${ASCCOMM_CANN_ARCH_DIR}/asc/include/basic_api
    ${ASCCOMM_CANN_ARCH_DIR}/asc/impl/basic_api
)

add_library(asccomm_hcomm_dfx_headers INTERFACE)
target_include_directories(asccomm_hcomm_dfx_headers INTERFACE
    ${ASCCOMM_DFX_PUBLIC_INCLUDE_DIRS}
)

add_library(asccomm_hcomm_dfx_obj OBJECT
    host/hcomm_dfx.cpp
    host/hcomm_dfx_exception.cpp
    host/hcomm_dfx_collect.cpp
    host/hcomm_dfx_parse.cpp
    host/hcomm_dfx_queue_reader.cpp
)
set_target_properties(asccomm_hcomm_dfx_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

set_source_files_properties(
    host/hcomm_dfx_queue_reader.cpp
    PROPERTIES LANGUAGE ASC
)

target_link_libraries(asccomm_hcomm_dfx_obj PRIVATE
    asccomm_hcomm_dfx_headers
)

target_include_directories(asccomm_hcomm_dfx_obj PRIVATE
    ${ASCCOMM_DFX_PRIVATE_INCLUDE_DIRS}
)
target_include_directories(asccomm_hcomm_dfx_obj SYSTEM PRIVATE
    ${ASCCOMM_DFX_CANN_INCLUDE_DIRS}
)
target_compile_definitions(asccomm_hcomm_dfx_obj PRIVATE
    BUILD_OPEN_PROJECT
)
target_compile_options(asccomm_hcomm_dfx_obj PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

add_library(asccomm_hcomm_dfx SHARED
    $<TARGET_OBJECTS:asccomm_hcomm_dfx_obj>
)
set_target_properties(asccomm_hcomm_dfx PROPERTIES
    OUTPUT_NAME asccomm_hcomm_dfx
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(asccomm_hcomm_dfx PRIVATE
    asccomm_hcomm_dfx_headers
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
    c_sec
)

if(NOT INSTALL_LIBRARY_DIR)
    set(INSTALL_LIBRARY_DIR lib64)
endif()
install(TARGETS asccomm_hcomm_dfx
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR} COMPONENT asccomm-hcomm-dfx
)

add_custom_target(asccomm_hcomm_dfx_compile_check DEPENDS asccomm_hcomm_dfx)
