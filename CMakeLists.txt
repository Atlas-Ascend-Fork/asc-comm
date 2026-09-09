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

include(version.cmake)
project(asc_comm VERSION "${ASCCOMM_VERSION}" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ASCCOMM_BUILD_CCU "Build asc-comm CCU launch wrapper library" ON)
# AICPU数据面(实体直入版)默认关闭：仅独立构建数据面时由build.sh显式开启。
# 第三方嵌入(如asc-devkit超构建，仅需CCU组件)不设置该选项，避免配置src/aicpu——
# 其对hcomm源码(pkg_inc契约头)的强校验会拦截无完整hcomm源的嵌入场景
option(ASCCOMM_BUILD_AICPU "Build asc-comm AICPU data plane library" OFF)

if(CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}")
elseif(ASCEND_CANN_PACKAGE_PATH)
    set(_ASCCOMM_CANN_PACKAGE_PATH "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_OPP_PATH})
    get_filename_component(_ASCCOMM_CANN_PACKAGE_PATH "$ENV{ASCEND_OPP_PATH}/.." ABSOLUTE)
else()
    set(_ASCCOMM_CANN_PACKAGE_PATH "/usr/local/Ascend/cann")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_ASCCOMM_CANN_PACKAGE_PATH}" CACHE PATH "CANN package path")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(_ASCCOMM_CANN_ARCH "aarch64-linux")
else()
    set(_ASCCOMM_CANN_ARCH "x86_64-linux")
endif()
set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}" CACHE PATH
    "Architecture-specific CANN package path")
if(ASCEND_CANN_PACKAGE_PATH AND NOT EXISTS "${ASCCOMM_CANN_ARCH_DIR}")
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux" CACHE PATH
        "Architecture-specific CANN package path" FORCE)
endif()

if(ASCCOMM_BUILD_CCU)
    add_subdirectory(src/ccu)
endif()
if(ASCCOMM_BUILD_AICPU)
    add_subdirectory(src/aicpu)
endif()
