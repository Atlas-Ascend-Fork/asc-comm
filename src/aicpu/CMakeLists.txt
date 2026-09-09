# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_AICPU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_AICPU_ROOT}/../.. ABSOLUTE)
set(ASCCOMM_DFX_SRC_DIR "${ASCCOMM_ROOT}/src/common/dfx")

if(NOT ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
    else()
        set(ASCEND_CANN_PACKAGE_PATH "/usr/local/Ascend/ascend-toolkit/latest")
    endif()
endif()
message(STATUS "ASCEND_CANN_PACKAGE_PATH = ${ASCEND_CANN_PACKAGE_PATH}")

if(NOT DEFINED ASCCOMM_CANN_ARCH_DIR)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
        set(_ASCCOMM_CANN_ARCH "aarch64-linux")
    else()
        set(_ASCCOMM_CANN_ARCH "x86_64-linux")
    endif()
endif()
set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}")
message(STATUS "ASCCOMM_CANN_ARCH_DIR = ${ASCCOMM_CANN_ARCH_DIR}")

# 查找 hcomm 代码：优先显式指定的 HCOMM_SOURCE_DIR(build.sh透传)与ASCCOMM_HCOMM_SOURCE_DIR，
# 其次探测样例仓同级目录(hcomm_pkg_inc/hcomm)，最后校验pkg_inc契约头存在
if(NOT ASCCOMM_HCOMM_SOURCE_DIR)
    if(HCOMM_SOURCE_DIR)
        set(ASCCOMM_HCOMM_SOURCE_DIR "${HCOMM_SOURCE_DIR}")
    endif()
endif()
if(NOT ASCCOMM_HCOMM_SOURCE_DIR)
    foreach(_candidate
        "${ASCCOMM_ROOT}/../hcomm_pkg_inc"
        "${ASCCOMM_ROOT}/../hcomm"
        "${CANN_3RD_LIB_PATH}/hcomm")
        if(EXISTS "${_candidate}/pkg_inc/hcomm/hcomm_pod_resource.h")
            get_filename_component(ASCCOMM_HCOMM_SOURCE_DIR "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
set(ASCCOMM_HCOMM_SOURCE_DIR "${ASCCOMM_HCOMM_SOURCE_DIR}" CACHE PATH "Original hcomm source tree")
if(NOT EXISTS "${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hcomm/hcomm_pod_resource.h")
    message(FATAL_ERROR
        "hcomm source not found (looked for pkg_inc/hcomm/hcomm_pod_resource.h under "
        "HCOMM_SOURCE_DIR/ASCCOMM_HCOMM_SOURCE_DIR=${ASCCOMM_HCOMM_SOURCE_DIR}, "
        "${ASCCOMM_ROOT}/../hcomm_pkg_inc, ${ASCCOMM_ROOT}/../hcomm). "
        "Set HCOMM_SOURCE_DIR to the HCOMM source root.")
endif()
message(STATUS "ASCCOMM_HCOMM_SOURCE_DIR = ${ASCCOMM_HCOMM_SOURCE_DIR}")

add_library(asc_comm_dfx INTERFACE)
target_include_directories(asc_comm_dfx INTERFACE ${ASCCOMM_DFX_SRC_DIR})

add_library(asccomm_aicpu SHARED)
add_subdirectory(hcomm)

target_compile_definitions(asccomm_aicpu PRIVATE
    HCCD
    CCL_KERNEL_AICPU
    OPEN_BUILD_PROJECT
    _GLIBCXX_USE_CXX11_ABI=1
)

target_compile_options(asccomm_aicpu PRIVATE
    -Wfloat-equal
    -Wall
    -fno-common
    -fstack-protector-strong
    -fno-strict-aliasing
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -pipe
    -O3
)

target_include_directories(asccomm_aicpu PRIVATE
    ${ASCCOMM_ROOT}/include/aicpu
    ${ASCCOMM_ROOT}/include/aicpu/hcomm
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc # hcomm 源码
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hcomm
    # CANN包公共依赖头(securec.h/dlog_pub.h/ascend_hal.h/acl头)，
    # 独立构建不依赖cann-cmake导入目标，直接取CANN包目录
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/driver
    ${ASCCOMM_AICPU_ROOT}/hcomm/primitives/api_c_adpt
    ${ASCCOMM_AICPU_ROOT}/hcomm/primitives/launch
    ${ASCCOMM_AICPU_ROOT}/hcomm/common/compat/hcomm
    ${ASCCOMM_AICPU_ROOT}/hcomm/common/compat/hcomm/hccl
    ${ASCCOMM_AICPU_ROOT}/hcomm/common/exception
    ${ASCCOMM_AICPU_ROOT}/hcomm/common/utils
    ${ASCCOMM_AICPU_ROOT}/hcomm/resource/buffer
    ${ASCCOMM_AICPU_ROOT}/hcomm/resource/common
    ${ASCCOMM_AICPU_ROOT}/hcomm/resource/connection
    ${ASCCOMM_AICPU_ROOT}/hcomm/resource/stream
)

# 设备侧库：securec/dlog/驱动符号由设备运行环境(ccl_kernel/内核驱动)在加载期解析，
# 链接期不强制落地；cann-cmake导入目标存在时按需附加，避免独立构建缺目标报错
set(_asccomm_aicpu_link_targets asc_comm_dfx)
foreach(_target acl_rt_headers slog_headers ascend_hal c_sec)
    if(TARGET ${_target})
        list(APPEND _asccomm_aicpu_link_targets ${_target})
    endif()
endforeach()
target_link_libraries(asccomm_aicpu PRIVATE
    ${_asccomm_aicpu_link_targets}
    dl
    pthread
)

if(NOT EXISTS "${ASCCOMM_ROOT}/cmake/asccomm_exports.map")
    message(WARNING "version script not found: ${ASCCOMM_ROOT}/cmake/asccomm_exports.map")
endif()
target_link_options(asccomm_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
    -Wl,--version-script=${ASCCOMM_ROOT}/cmake/asccomm_exports.map
)

set_target_properties(asccomm_aicpu PROPERTIES SKIP_BUILD_RPATH TRUE)
