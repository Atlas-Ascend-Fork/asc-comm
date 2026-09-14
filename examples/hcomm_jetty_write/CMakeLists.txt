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

if(NOT CMAKE_ASC_ARCHITECTURES STREQUAL "dav-3510")
    message(FATAL_ERROR "hcomm_jetty_write only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(hcomm_jetty_write LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_source_files_properties(
    hcomm_jetty_write.asc
    PROPERTIES LANGUAGE ASC
)

add_executable(hcomm_jetty_write
    hcomm_jetty_write.asc
)

target_include_directories(hcomm_jetty_write PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/aicore
)

target_link_directories(hcomm_jetty_write PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
)

target_link_libraries(hcomm_jetty_write PRIVATE
    -Wl,--no-as-needed
    hccl
    hcomm
    -Wl,--as-needed
    ascendcl
    acl_rt
    runtime
    stdc++
    c_sec
    dl
    pthread
)


target_compile_options(hcomm_jetty_write PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:--cce-auto-infer-kernel-type=false>
)

# ASC eager kernel registration contains absolute references to the embedded device binary.
target_link_options(hcomm_jetty_write PRIVATE -no-pie)

set_target_properties(hcomm_jetty_write PROPERTIES
    BUILD_RPATH "${ASCEND_CANN_PACKAGE_PATH}/lib64;${ASCEND_CANN_PACKAGE_LINUX_PATH}/lib64"
)
