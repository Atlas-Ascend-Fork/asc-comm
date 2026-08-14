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
    message(FATAL_ERROR "simt_write only supports dav-3510, but got ${CMAKE_ASC_ARCHITECTURES}")
endif()

find_package(ASC REQUIRED)

project(simt_write LANGUAGES ASC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only the kernel is compiled as ASC. main.cpp is plain host C++ talking to HCCL, so keeping it
# on the CXX toolchain avoids pulling the device compiler into the host translation unit.
set_source_files_properties(
    op_kernel.cpp
    simt_write_common.h
    PROPERTIES LANGUAGE ASC
)

add_executable(simt_write
    main.cpp
    op_kernel.cpp
    simt_write_common.h
)

target_include_directories(simt_write BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $ENV{ASCEND_HOME_PATH}/include
    $<$<COMPILE_LANGUAGE:ASC>:$ENV{ASCEND_HOME_PATH}/asc/include/comm_api>
)

# 链接 hcomm 库：Host 侧通信域创建(HcclCommInitClusterInfo)、内存注册(HcclCommMemReg)、
# 通道创建(HcclChannelAcquire)等接口由 libhcomm.so 提供。
target_link_libraries(simt_write PRIVATE
    hcomm
)

target_compile_options(simt_write PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
)
