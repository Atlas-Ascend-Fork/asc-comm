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
project(hcomm_roce_multi_sge LANGUAGES ASC CXX)

find_package(MPI REQUIRED COMPONENTS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_source_files_properties(roce_multi_sge_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(hcomm_roce_multi_sge
    roce_multi_sge.asc
    roce_multi_sge_kernel.cpp
)

set_target_properties(hcomm_roce_multi_sge PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(hcomm_roce_multi_sge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPI_CXX_INCLUDE_DIRS}
    $ENV{ASCEND_HOME_PATH}/include
)

target_link_directories(hcomm_roce_multi_sge PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(hcomm_roce_multi_sge PRIVATE
    MPI::MPI_CXX
    hcomm
    ascendcl
    runtime
)

target_compile_options(hcomm_roce_multi_sge PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)

option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)
if(ASCENDC_DEBUG)
    target_compile_definitions(hcomm_roce_multi_sge PRIVATE
        ASCENDC_DEBUG
    )
endif()
