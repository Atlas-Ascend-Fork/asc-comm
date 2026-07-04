# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
#
# A5 AICPU 数据面模块（迁移自 hcomm 仓）。
#   - 头文件根：include/aicpu/hcomm/（保留原 hcomm 目录层级：ascend950/ ascend910/ 等）
#   - 源文件根：src/aicpu/hcomm/
# 说明：本模块只包含 A5（Ascend 950）AICPU 数据面本体及其真实依赖，
#       非 A5 的 algorithm/framework 框架层已在迁移时剥离，由 hccl_compat 兼容层顶替。

cmake_minimum_required(VERSION 3.16)
project(asccomm_aicpu_hcomm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 仓根：本文件位于 <ROOT>/src/aicpu/hcomm/CMakeLists.txt
get_filename_component(ASCCOMM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(HCOMM_INC_ROOT  "${ASCCOMM_ROOT}/include/aicpu/hcomm")
set(HCOMM_SRC_ROOT  "${ASCCOMM_ROOT}/src/aicpu/hcomm")
set(HCOMM_COMPAT_ROOT "${ASCCOMM_ROOT}/src/aicpu/common/hccl_compat")

# ---------------------------------------------------------------------------
# 1. 头文件包含根：递归收集 include/aicpu/hcomm 下所有子目录作为 -I 根，
#    复刻原 hcomm 对 ascend950 的 GLOB 方式，避免逐条枚举几十个子目录。
# ---------------------------------------------------------------------------
set(HCOMM_INC_DIRS "")
file(GLOB_RECURSE _all_inc_entries LIST_DIRECTORIES TRUE "${HCOMM_INC_ROOT}/*")
list(APPEND HCOMM_INC_DIRS "${HCOMM_INC_ROOT}")
foreach(_e ${_all_inc_entries})
    if(IS_DIRECTORY "${_e}")
        list(APPEND HCOMM_INC_DIRS "${_e}")
    endif()
endforeach()
# 兼容层（hccl_compat）头根：顶替非 A5 框架层符号
if(EXISTS "${HCOMM_COMPAT_ROOT}")
    file(GLOB_RECURSE _compat_entries LIST_DIRECTORIES TRUE "${HCOMM_COMPAT_ROOT}/*")
    list(APPEND HCOMM_INC_DIRS "${HCOMM_COMPAT_ROOT}")
    foreach(_e ${_compat_entries})
        if(IS_DIRECTORY "${_e}")
            list(APPEND HCOMM_INC_DIRS "${_e}")
        endif()
    endforeach()
endif()
list(REMOVE_DUPLICATES HCOMM_INC_DIRS)

# ---------------------------------------------------------------------------
# 2. 源文件：收集 src/aicpu/hcomm 下全部 .cc
# ---------------------------------------------------------------------------
file(GLOB_RECURSE HCOMM_A5_SOURCES CONFIGURE_DEPENDS "${HCOMM_SRC_ROOT}/*.cc")
# 兼容层实现（若有）
if(EXISTS "${HCOMM_COMPAT_ROOT}")
    file(GLOB_RECURSE HCOMM_COMPAT_SOURCES CONFIGURE_DEPENDS "${HCOMM_COMPAT_ROOT}/*.cc")
    list(APPEND HCOMM_A5_SOURCES ${HCOMM_COMPAT_SOURCES})
endif()

# ---------------------------------------------------------------------------
# 3. CANN / 驱动外部依赖头根（acl_rt.h / ascend_hal.h / securec.h / prof 等）
#    复用 tests/ut 的 CANN 包探测约定。
# ---------------------------------------------------------------------------
if(DEFINED CUSTOM_ASCEND_CANN_PACKAGE_PATH)
    set(_CANN "${CUSTOM_ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ASCEND_CANN_PACKAGE_PATH)
    set(_CANN "${ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
    set(_CANN "$ENV{ASCEND_CANN_PACKAGE_PATH}")
elseif(DEFINED ENV{ASCEND_HOME_PATH})
    set(_CANN "$ENV{ASCEND_HOME_PATH}")
else()
    set(_CANN "/usr/local/Ascend/cann")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${_CANN}" CACHE PATH "CANN package path")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(_CANN_ARCH "aarch64-linux")
else()
    set(_CANN_ARCH "x86_64-linux")
endif()
set(ASCEND_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_CANN_ARCH}")

set(CANN_INC_DIRS
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_ARCH_DIR}/include
    ${ASCEND_CANN_ARCH_DIR}/include/acl
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/mmpa
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/profiling
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime
    ${ASCEND_CANN_ARCH_DIR}/pkg_inc/runtime/runtime
)

# ---------------------------------------------------------------------------
# 4. 目标库：ccl_kernel（A5 AICPU 数据面 Kernel）
# ---------------------------------------------------------------------------
add_library(ccl_kernel STATIC ${HCOMM_A5_SOURCES})

target_include_directories(ccl_kernel PUBLIC
    ${HCOMM_INC_DIRS}
    ${CANN_INC_DIRS}
)

target_compile_definitions(ccl_kernel PRIVATE
    CCL_KERNEL_AICPU        # A5 AICPU 数据面编译开关（eager/batch 判定、Get*TimeOut 偏移等依赖它）
)

target_compile_options(ccl_kernel PRIVATE
    -fPIC
    -Wno-unused-parameter
)

# 供上层/测试引用的别名
add_library(asccomm::aicpu_hcomm ALIAS ccl_kernel)

message(STATUS "[aicpu/hcomm] sources: ${HCOMM_SRC_ROOT}")
message(STATUS "[aicpu/hcomm] include roots: ${HCOMM_INC_ROOT} (+ recursive subdirs, compat, CANN)")
