# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16.0)

# 默认只编译 host 侧
if(NOT DEFINED PRODUCT_SIDE)
    set(PRODUCT_SIDE "host")
endif()

set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}" CACHE PATH "CANN toolkit install path")

# 引入CANN/cmake库
include(cmake/fetch_cann_cmake.cmake)
project(hccl)

# 初始化 CANN 项目配置
init_cann_project(PREPEND_MODULE_PATH)
add_cann_target_options()
# 采用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")

if(ENABLE_CUSTOM)
    # 自定义(AICPU)算子打包
    if(NOT CUSTOM_OPS_NAME)
        message(FATAL_ERROR "Custom ops name is not set")
    endif()
    if(NOT CUSTOM_OPS_PATH)
        message(FATAL_ERROR "Custom ops path is not set")
    endif()
    if(NOT CUSTOM_OPS_VENDOR)
        message(FATAL_ERROR "Custom ops vendor is not set")
    endif()

    if(NOT EXISTS ${CUSTOM_OPS_PATH})
        message(FATAL_ERROR "Custom ops path does not exist: ${CUSTOM_OPS_PATH}")
    endif()
    if(NOT EXISTS "${CUSTOM_OPS_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "CMakeLists.txt not found. Please ensure it's a CMake project: ${CUSTOM_OPS_PATH}")
    endif()

    message(STATUS "Custom ops name: ${CUSTOM_OPS_NAME}")
    message(STATUS "Custom ops vendor: ${CUSTOM_OPS_VENDOR}")
    message(STATUS "Custom ops path: ${CUSTOM_OPS_PATH}")

    set(BUILD_DEVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build_device)
    set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build)
    set(CUSTOM_OPS_OPP_PATH "opp/vendors/${CUSTOM_OPS_VENDOR}")
    set(CUSTOM_OPS_OPP_INC_PATH "${CUSTOM_OPS_OPP_PATH}/include")
    set(CUSTOM_OPS_OPP_LIB_PATH "${CUSTOM_OPS_OPP_PATH}/lib64")
    set(CUSTOM_OPS_OPP_SCRIPTS_PATH "${CUSTOM_OPS_OPP_PATH}/scripts")
    set(CUSTOM_OPS_OPP_AICPU_CONFIG_PATH "${CUSTOM_OPS_OPP_PATH}/aicpu/config")
    set(CUSTOM_OPS_OPP_AICPU_KERNEL_PATH "${CUSTOM_OPS_OPP_PATH}/aicpu/kernel")

    include(cmake/func.cmake)
    include(cmake/package.cmake)
    # hccl_ops follows the native HCCL example layout and only uses the HCOMM
    # primitives shipped with CANN. It must not require the asc-comm POD setup.
    get_filename_component(_CUSTOM_OPS_ROOT_DIR "${CUSTOM_OPS_PATH}" DIRECTORY)
    get_filename_component(_CUSTOM_OPS_ROOT_NAME "${_CUSTOM_OPS_ROOT_DIR}" NAME)
    if(NOT _CUSTOM_OPS_ROOT_NAME STREQUAL "hccl_ops")
        include(cmake/hcomm_pod.cmake)
    endif()

    # AICPU Device 构建目标
    add_custom_target(custom_aicpu)
    # CPack 打包
    pack_custom()
    # 添加自定义算子工程
    add_subdirectory(${CUSTOM_OPS_PATH})
endif()
