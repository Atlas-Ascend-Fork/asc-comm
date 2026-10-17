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
    message(FATAL_ERROR "rdv_hcomm only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)
project(rdv_hcomm LANGUAGES ASC CXX)

set(RDV_AIV_CORE_NUM "1" CACHE STRING "Number of AIV cores used by rdv_hcomm")
if(NOT RDV_AIV_CORE_NUM MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "RDV_AIV_CORE_NUM must be a positive integer")
endif()
if(RDV_AIV_CORE_NUM GREATER 56)
    message(FATAL_ERROR "RDV_AIV_CORE_NUM must not exceed 56")
endif()

set_source_files_properties(rdv_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(rdv_hcomm
    rdv_hcomm.asc
    rdv_kernel.cpp
)

set_target_properties(rdv_hcomm PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(rdv_hcomm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{ASCEND_HOME_PATH}/asc/include/comm_api
)

target_link_directories(rdv_hcomm PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(rdv_hcomm PRIVATE
    hcomm
    hccl
)

target_compile_options(rdv_hcomm PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

target_compile_definitions(rdv_hcomm PRIVATE
    RDV_AIV_CORE_NUM=${RDV_AIV_CORE_NUM}U
)
