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
set(CANN_HOME "$ENV{ASCEND_HOME_PATH}")

list(PREPEND CAMKE_PREFIX_PATH "${CANN_HOME}/lib64/cmake")

find_package(ASC REQUIRED)

project(hcomm_roce_write_read_nbi LANGUAGES ASC CXX)

find_package(MPI REQUIRED COMPONENTS CXX)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(CANN_ARCH_DIR "aarch64-linux")
else()
    set(CANN_ARCH_DIR "x86_64-linux")
endif()

option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)

set_source_files_properties(hcomm_roce_write_read_nbi_kernel.cpp PROPERTIES LANGUAGE ASC)

add_executable(hcomm_roce_write_read_nbi
    main.cpp
    hcomm_roce_write_read_nbi_kernel.cpp
)

set_target_properties(hcomm_roce_write_read_nbi PROPERTIES
    LINKER_LANGUAGE ASC
)

target_include_directories(hcomm_roce_write_read_nbi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CANN_HOME}/include
    ${CANN_HOME}/${CANN_ARCH_DIR}/include
)

target_link_directories(hcomm_roce_write_read_nbi PRIVATE
    ${CANN_HOME}/lib64
    ${CANN_HOME}/${CANN_ARCH_DIR}/lib64
)

target_link_libraries(hcomm_roce_write_read_nbi PRIVATE
    MPI::MPI_CXX
    hcomm
    ascendcl
    runtime
)

target_compile_features(hcomm_roce_write_read_nbi PRIVATE cxx_std_17)

target_compile_options(hcomm_roce_write_read_nbi PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)

if(ASCENDC_DEBUG)
    target_compile_definitions(hcomm_roce_write_read_nbi PRIVATE
        ASCENDC_DEBUG
    )
endif()
