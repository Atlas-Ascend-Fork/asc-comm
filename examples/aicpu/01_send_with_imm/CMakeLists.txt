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
find_package(AICPU REQUIRED)
project(asccomm_send_with_imm LANGUAGES ASC AICPU CXX)

if(NOT DEFINED ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH $ENV{ASCEND_HOME_PATH})
    elseif(DEFINED ENV{ASCEND_OPP_PATH})
        get_filename_component(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
    else()
        message(FATAL_ERROR "ASCEND_CANN_PACKAGE_PATH or ASCEND_HOME_PATH must be set")
    endif()
endif()

get_filename_component(ASC_COMM_EXAMPLES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(ASC_COMM_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# asc-comm AICPU 数据面头文件：优先取已安装的 pkg_inc/asccomm，其次取本仓源码树。
set(ASCCOMM_AICPU_INCLUDE_DIR "")
foreach(_candidate
    "${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/asccomm"
    "${ASC_COMM_REPO_ROOT}/include/aicpu/hcomm")
    if(EXISTS "${_candidate}/asccomm_primitives.h")
        set(ASCCOMM_AICPU_INCLUDE_DIR "${_candidate}")
        break()
    endif()
endforeach()
if(NOT ASCCOMM_AICPU_INCLUDE_DIR)
    message(FATAL_ERROR
        "asccomm_primitives.h not found; install the asc-comm run package or build from the asc-comm source tree")
endif()
message(STATUS "ASCCOMM_AICPU_INCLUDE_DIR = ${ASCCOMM_AICPU_INCLUDE_DIR}")

add_executable(asccomm_send_with_imm
    send_with_imm.asc
    send_with_imm_kernel.aicpu
)

set_target_properties(asccomm_send_with_imm PROPERTIES
    LINKER_LANGUAGE ASC
)

# ASCCOMM_AICPU_INCLUDE_DIR 必须排在 CANN 包头之前：本仓 include/aicpu/hcomm 下的
# hcomm_res_entity_defs.h 是数据面依赖的 POD v2 契约，CANN 包 pkg_inc/hcomm 内的同名头是旧版本。
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(ASC_COMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux")
else()
    set(ASC_COMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/x86_64-linux")
endif()

target_include_directories(asccomm_send_with_imm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ASCCOMM_AICPU_INCLUDE_DIR}
    ${ASC_COMM_EXAMPLES_ROOT}/aicore
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASC_COMM_CANN_ARCH_DIR}/include
)

target_compile_options(asccomm_send_with_imm PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    # AICPU Kernel 由 Host 侧 <<<>>> 直调下发，需要该实验性选项。
    --cce-aicpu-launch-with-interface
)

target_link_directories(asccomm_send_with_imm PRIVATE
    $ENV{ASCEND_HOME_PATH}/lib64
    $ENV{ASCEND_HOME_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux/lib64
)

# asccomm_aicpu 是 AICPU 设备侧库，符号在设备运行环境加载期解析。
target_link_libraries(asccomm_send_with_imm PRIVATE
    hcomm
    hccl
    asccomm_aicpu
)
