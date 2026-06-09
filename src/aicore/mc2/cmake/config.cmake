# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set(DEFAULT_BUILD_TYPE "Release")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the build type: Release/Debug" FORCE)
endif()


if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(HCCL_HOST_ARCH_PREFIX x86_64-linux)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(HCCL_HOST_ARCH_PREFIX aarch64-linux)
else()
    message(FATAL_ERROR "Unsupported host architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()


set(ASCEND_MOCKCPP_PACKAGE_PATH ${CMAKE_CURRENT_SOURCE_DIR})


if (CMAKE_INSTALL_PREFIX STREQUAL /usr/local)
    set(CMAKE_INSTALL_PREFIX     "${HCCL_CC_DIR}/output"  CACHE STRING "path for install()" FORCE)
endif ()

set(HI_PYTHON                     "python3"                       CACHE   STRING   "python executor")

message(STATUS "config.cmake KERNEL_MODE=${KERNEL_MODE} BUILD_OPEN_PROJECT=${BUILD_OPEN_PROJECT}")
if(BUILD_OPEN_PROJECT AND KERNEL_MODE)
    set(PRODUCT_SIDE                  device)
else()
    set(PRODUCT_SIDE                  host)
endif()

set(HCCL_C_SEC_LINK c_sec)
set(HCCL_MMPA_LINK mmpa)
set(HCCL_UNIFIED_DLOG_LINK unified_dlog)

set(INSTALL_LIBRARY_DIR ${CMAKE_SYSTEM_PROCESSOR}-linux/lib64)
set(INSTALL_AICPU_KERNEL_JSON_DIR opp/built-in/op_impl/aicpu/config)
set(INSTALL_DEVICE_TAR_DIR opp/built-in/op_impl/aicpu/kernel)
set(INSTALL_PKG_INCLUDE_DIR ${CMAKE_SYSTEM_PROCESSOR}-linux/pkg_inc)
set(INSTALL_CCL_KERNEL_JSON_DIR opp/built-in/op_impl/aicpu/config)
set(INSTALL_DPU_KERNEL_JSON_DIR opp/built-in/op_impl/dpu/config)
if (ENABLE_TEST)
    set(CMAKE_SKIP_RPATH FALSE)
else ()
    set(CMAKE_SKIP_RPATH TRUE)
endif ()
