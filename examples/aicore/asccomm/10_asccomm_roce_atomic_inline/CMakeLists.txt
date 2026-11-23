# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

set(CMAKE_ASC_ARCHITECTURES "dav-3510" CACHE STRING "NPU architecture: dav-3510")

find_package(ASC REQUIRED)
project(asccomm_roce_atomic_inline LANGUAGES ASC CXX)
find_package(MPI REQUIRED COMPONENTS CXX)

set_source_files_properties(roce_atomic_inline_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(asccomm_roce_atomic_inline
    roce_atomic_inline.asc
    roce_atomic_inline_kernel.cpp
)

set_target_properties(asccomm_roce_atomic_inline PROPERTIES
    LINKER_LANGUAGE ASC
)

set(ASCCOMM_PACKAGE_INCLUDE_DIR "$ENV{ASCEND_HOME_PATH}/asc/include/comm_api/aicore")
target_include_directories(asccomm_roce_atomic_inline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_PACKAGE_INCLUDE_DIR}
    ${MPI_CXX_INCLUDE_DIRS}
    $ENV{ASCEND_HOME_PATH}/include
)

target_link_directories(asccomm_roce_atomic_inline PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(asccomm_roce_atomic_inline PRIVATE
    MPI::MPI_CXX
    hcomm
    ascendcl
    runtime
)

target_compile_features(asccomm_roce_atomic_inline PRIVATE cxx_std_17)

target_compile_options(asccomm_roce_atomic_inline PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)

option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)
if(ASCENDC_DEBUG)
    target_compile_definitions(asccomm_roce_atomic_inline PRIVATE
        ASCENDC_DEBUG
    )
endif()

option(ASCCOMM_ROCE_BACKEND_MLX5 "Enable mlx5" ON)
if(ASCCOMM_ROCE_BACKEND_MLX5)
    target_compile_definitions(asccomm_roce_atomic_inline PRIVATE
        ASCCOMM_ROCE_BACKEND_MLX5
    )
endif()
