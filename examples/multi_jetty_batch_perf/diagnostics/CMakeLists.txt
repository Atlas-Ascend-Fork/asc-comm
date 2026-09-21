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

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture")
find_package(ASC REQUIRED)
project(multi_jetty_batch_perf LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(MULTI_JETTY_BACKEND "all" CACHE STRING "Kernel backends: all, simt, or simd")
set_property(CACHE MULTI_JETTY_BACKEND PROPERTY STRINGS all simt simd)
if(NOT MULTI_JETTY_BACKEND MATCHES "^(all|simt|simd)$")
    message(FATAL_ERROR "MULTI_JETTY_BACKEND must be all, simt, or simd")
endif()
option(MULTI_JETTY_DIAGNOSTIC_FAST_BUILD "Build only the current SIMT parallel diagnostic matrix" OFF)
if(MULTI_JETTY_BACKEND STREQUAL "simd" AND MULTI_JETTY_DIAGNOSTIC_FAST_BUILD)
    message(FATAL_ERROR "Fast mode only applies to SIMT; use the simd-full profile")
endif()

set(MULTI_JETTY_SCOPE_EXPERIMENT "none" CACHE STRING "Isolated scope experiment: none, control, publisher, sq-prepare, parallel-publish, publish-core, publish-audit, matrix")
set_property(CACHE MULTI_JETTY_SCOPE_EXPERIMENT PROPERTY STRINGS none control publisher sq-prepare parallel-publish publish-core publish-audit matrix)
set(MULTI_JETTY_MATRIX_BATCH "0" CACHE STRING "Matrix profile batch size (one VF per build)")
set(scope_build 0)
if(NOT MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "none")
    if(NOT MULTI_JETTY_BACKEND STREQUAL "simt" OR MULTI_JETTY_DIAGNOSTIC_FAST_BUILD)
        message(FATAL_ERROR "Scope experiments require SIMT only, with fast mode OFF")
    endif()
    if(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "control")
        set(scope_build 1)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "publisher")
        set(scope_build 2)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "sq-prepare")
        set(scope_build 3)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "parallel-publish")
        set(scope_build 4)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "publish-core")
        set(scope_build 5)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "publish-audit")
        set(scope_build 6)
    elseif(MULTI_JETTY_SCOPE_EXPERIMENT STREQUAL "matrix")
        if(NOT MULTI_JETTY_MATRIX_BATCH MATCHES "^(1|8|32|64|128|256)$")
            message(FATAL_ERROR "Matrix requires one batch size: 1,8,32,64,128,256")
        endif()
        set(scope_build 7)
    else()
        message(FATAL_ERROR "Scope experiment must be none, control, publisher, sq-prepare, parallel-publish, publish-core, or publish-audit")
    endif()
endif()

set(kernel_sources)
set(enable_simd 0)
set(enable_simt 0)
if(NOT MULTI_JETTY_BACKEND STREQUAL "simt")
    list(APPEND kernel_sources simd_kernel.cpp)
    set(enable_simd 1)
endif()
if(NOT MULTI_JETTY_BACKEND STREQUAL "simd")
    list(APPEND kernel_sources simt_kernel.cpp)
    set(enable_simt 1)
    set_property(SOURCE simt_kernel.cpp APPEND PROPERTY COMPILE_DEFINITIONS USE_SIMT)
endif()
set_source_files_properties(${kernel_sources} PROPERTIES LANGUAGE ASC)
add_executable(multi_jetty_batch_perf main.cpp ${kernel_sources})
target_compile_definitions(multi_jetty_batch_perf PRIVATE
    MULTI_JETTY_ENABLE_SIMD=${enable_simd}
    MULTI_JETTY_ENABLE_SIMT=${enable_simt}
    MULTI_JETTY_BUILD_BACKEND="${MULTI_JETTY_BACKEND}"
    MULTI_JETTY_BUILD_FAST=$<BOOL:${MULTI_JETTY_DIAGNOSTIC_FAST_BUILD}>
    MULTI_JETTY_SCOPE_BUILD=${scope_build}
    MULTI_JETTY_MATRIX_BATCH=${MULTI_JETTY_MATRIX_BATCH}
    MULTI_JETTY_BUILD_SCOPE="${MULTI_JETTY_SCOPE_EXPERIMENT}"
)
if(MULTI_JETTY_DIAGNOSTIC_FAST_BUILD)
    set_property(SOURCE simt_kernel.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS MULTI_JETTY_DIAGNOSTIC_FAST_BUILD)
endif()
message(STATUS "Multi-Jetty: backend=${MULTI_JETTY_BACKEND}, fast=${MULTI_JETTY_DIAGNOSTIC_FAST_BUILD}")
set_target_properties(multi_jetty_batch_perf PROPERTIES LINKER_LANGUAGE ASC)
target_include_directories(multi_jetty_batch_perf BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{ASCEND_HOME_PATH}/include
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/adv_api>
)
# Only the completion-fix profile uses the current checkout's Hcomm headers. Preserve the
# installed include/impl relationship without overwriting the SDK or building the full package.
if(scope_build GREATER_EQUAL 6)
    get_filename_component(comm_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
    set(comm_headers "${CMAKE_CURRENT_BINARY_DIR}/comm-headers")
    file(MAKE_DIRECTORY "${comm_headers}/include/adv_api" "${comm_headers}/impl/comm_api/aicore")
    file(COPY "${comm_source_root}/include/aicore/hcomm" DESTINATION "${comm_headers}/include/adv_api")
    file(COPY "${comm_source_root}/src/aicore/hcomm" DESTINATION "${comm_headers}/impl/comm_api/aicore")
    target_include_directories(multi_jetty_batch_perf BEFORE PRIVATE "${comm_headers}/include/adv_api")
    # Bind the entry header explicitly: the ASC compiler may add SDK search paths
    # when compiling its internal host/device translation units.
    set_property(SOURCE simt_kernel.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MULTI_JETTY_HCOMM_SIMT_HEADER="${comm_headers}/include/adv_api/hcomm/hcomm_simt.h")
    message(STATUS "Completion headers: ${comm_source_root} -> ${comm_headers}")
endif()

target_link_directories(multi_jetty_batch_perf PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)
target_link_libraries(multi_jetty_batch_perf PRIVATE hcomm hccl ascendcl acl_rt runtime c_sec dl pthread)
target_compile_options(multi_jetty_batch_perf PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-res-usage>
)
