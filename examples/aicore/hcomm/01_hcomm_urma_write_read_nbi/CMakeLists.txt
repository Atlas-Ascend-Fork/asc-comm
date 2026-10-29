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

find_package(ASC REQUIRED)
project(hcomm_write_read_nbi LANGUAGES ASC CXX)

option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)

set_source_files_properties(hcomm_write_read_nbi_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(hcomm_write_read_nbi
    hcomm_write_read_nbi.asc
    hcomm_write_read_nbi_kernel.cpp
)

set_target_properties(hcomm_write_read_nbi PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(hcomm_write_read_nbi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_directories(hcomm_write_read_nbi PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(hcomm_write_read_nbi PRIVATE
    hcomm
    hccl
)

target_compile_options(hcomm_write_read_nbi PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)

if(ASCENDC_DEBUG)
    target_compile_definitions(hcomm_write_read_nbi PRIVATE
        ASCENDC_DEBUG
    )
endif()
