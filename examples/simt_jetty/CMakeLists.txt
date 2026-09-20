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

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture: dav-3510")
if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "simt_jetty only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(simt_jetty LANGUAGES ASC CXX)

option(HCOMM_CCE_RES_USAGE "Print CCE stack and register usage" OFF)
set(SIMT_JETTY_SGE_NUM "12" CACHE STRING "Compile-time SGE count for thread and warp modes (1 to 12)")
if(NOT SIMT_JETTY_SGE_NUM MATCHES "^[1-9]$|^1[0-2]$")
    message(FATAL_ERROR "SIMT_JETTY_SGE_NUM must be an integer in [1, 12]")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set_source_files_properties(op_kernel.cpp PROPERTIES LANGUAGE ASC)

# The public headers reach their implementation through package-layout relative paths
# (../../../impl/comm_api/...), which only resolve in an installed tree. Stage a
# package-shaped header tree from the working copy so this sample builds against the
# repository instead of whatever asc-comm version happens to be installed.
set(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(ASCCOMM_STAGE_ROOT "${CMAKE_BINARY_DIR}/package_layout")
# Mirror the installed depth exactly. Installed, the public headers live at
# asc/include/adv_api/<mod>/ and are also reachable via the asc/include/comm_api/aicore/<mod>
# symlink; their "../../../impl/comm_api/..." includes resolve relative to the real adv_api
# location, i.e. to asc/impl/. Staging the headers directly under comm_api/aicore would make
# that same path resolve to asc/include/impl/ and fail, so replicate the adv_api layout.
set(ASCCOMM_STAGE_INCLUDE_DIR "${ASCCOMM_STAGE_ROOT}/asc/include/adv_api")
set(ASCCOMM_STAGE_IMPL_DIR "${ASCCOMM_STAGE_ROOT}/asc/impl/comm_api/aicore")
set(ASCCOMM_STAGE_STAMP "${ASCCOMM_STAGE_ROOT}/.header_layout.stamp")

file(GLOB_RECURSE ASCCOMM_STAGE_INPUTS CONFIGURE_DEPENDS
    "${ASCCOMM_ROOT}/include/aicore/*"
    "${ASCCOMM_ROOT}/src/aicore/*"
)

add_custom_command(
    OUTPUT ${ASCCOMM_STAGE_STAMP}
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASCCOMM_STAGE_ROOT}/asc"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/hcomm" "${ASCCOMM_STAGE_INCLUDE_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/include/aicore/jetty" "${ASCCOMM_STAGE_INCLUDE_DIR}/jetty"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/hcomm" "${ASCCOMM_STAGE_IMPL_DIR}/hcomm"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/jetty" "${ASCCOMM_STAGE_IMPL_DIR}/jetty"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ASCCOMM_ROOT}/src/aicore/utils" "${ASCCOMM_STAGE_IMPL_DIR}/utils"
    # The ASC driver prepends its own search paths (including the installed asc/include/adv_api)
    # ahead of every -I, so an "#include \"hcomm/...\"" always finds the installed copy first and
    # no -I ordering can override it. Expose the staged headers under a prefix that exists
    # nowhere in the driver's builtin paths. It is a symlink to the staged adv_api dir, so the
    # public headers' "../../../impl/..." includes still resolve from the real location.
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "adv_api" "${ASCCOMM_STAGE_ROOT}/asc/include/asccomm_staged"
    COMMAND ${CMAKE_COMMAND} -E touch ${ASCCOMM_STAGE_STAMP}
    DEPENDS ${ASCCOMM_STAGE_INPUTS}
    COMMENT "Preparing package-shaped asc-comm headers for simt_jetty"
)
add_custom_target(simt_jetty_header_layout DEPENDS ${ASCCOMM_STAGE_STAMP})

add_executable(simt_jetty
    main.cpp
    op_kernel.cpp
)

add_dependencies(simt_jetty simt_jetty_header_layout)

target_compile_definitions(simt_jetty PRIVATE
    USE_SIMT
    SIMT_JETTY_SGE_NUM=${SIMT_JETTY_SGE_NUM}
)

set_target_properties(simt_jetty PROPERTIES LINKER_LANGUAGE ASC)

target_include_directories(simt_jetty BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_STAGE_ROOT}/asc/include
    $ENV{ASCEND_HOME_PATH}/include
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/asc
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/asc/include
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/asc/include/basic_api
    # Deliberately NOT adding ${ASCEND_HOME_PATH}/asc/include/adv_api here: it carries an
    # installed copy of the Hcomm/Jetty headers that shadows the staged tree above regardless
    # of -I order. kernel_operator.h and friends resolve from asc/include instead.
)

target_link_directories(simt_jetty PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(simt_jetty PRIVATE
    hcomm
    hccl
    ascendcl
    acl_rt
    runtime
    c_sec
    dl
    pthread
)

target_compile_options(simt_jetty PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
    $<$<AND:$<COMPILE_LANGUAGE:ASC>,$<BOOL:${HCOMM_CCE_RES_USAGE}>>:--cce-res-usage>
)
