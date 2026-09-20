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

project(hcomm_roce_write_read_nbi LANGUAGES ASC CXX)

find_package(MPI REQUIRED COMPONENTS CXX)

option(ASCENDC_DEBUG "Enable AscendC/Hcomm kernel debug logs" OFF)

# The repository Hcomm public header expects the installed-package directory
# layout. Stage the source headers into that layout in the build directory so
# this example can test interfaces that are newer than the installed toolkit.
set(ASCCOMM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../")
set(HCOMM_STAGE_ROOT "${CMAKE_BINARY_DIR}/package_layout")
set(HCOMM_STAGE_INCLUDE_ROOT "${HCOMM_STAGE_ROOT}/asc/include/adv_api")
set(HCOMM_STAGE_IMPL_ROOT "${HCOMM_STAGE_ROOT}/asc/impl/comm_api/aicore")

file(REMOVE_RECURSE "${HCOMM_STAGE_ROOT}")
file(MAKE_DIRECTORY
    "${HCOMM_STAGE_INCLUDE_ROOT}"
    "${HCOMM_STAGE_ROOT}/asc/include/comm_api/aicore"
    "${HCOMM_STAGE_IMPL_ROOT}")
file(COPY "${ASCCOMM_ROOT}/include/aicore/hcomm" DESTINATION "${HCOMM_STAGE_INCLUDE_ROOT}")
file(COPY "${ASCCOMM_ROOT}/src/aicore/hcomm" DESTINATION "${HCOMM_STAGE_IMPL_ROOT}")
file(CREATE_LINK "../../adv_api/hcomm"
    "${HCOMM_STAGE_ROOT}/asc/include/comm_api/aicore/hcomm" SYMBOLIC)

# The ASC compiler resolves "hcomm/hcomm.h" through its own built-in include
# table. Generate an absolute-include wrapper so this example always tests the
# repository version staged above.
set(HCOMM_PUBLIC_HEADER_WRAPPER "${CMAKE_BINARY_DIR}/hcomm_public_header.h")
file(WRITE "${HCOMM_PUBLIC_HEADER_WRAPPER}" "#include \"${HCOMM_STAGE_INCLUDE_ROOT}/hcomm/hcomm.h\"\n")

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
    ${CMAKE_BINARY_DIR}
    $ENV{ASCEND_HOME_PATH}/include
    $ENV{ASCEND_HOME_PATH}/aarch64-linux/include
)

target_link_directories(hcomm_roce_write_read_nbi PRIVATE
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

target_link_libraries(hcomm_roce_write_read_nbi PRIVATE
    MPI::MPI_CXX
    hcomm
    hccl
    ascendcl
    acl_rt
    runtime
    c_sec
    dl
    pthread
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
