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
set(ASCCOMM_DFX_SRC_DIR "${ASCCOMM_ROOT}/src/common/dfx/logging")

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

if(NOT ASCCOMM_CANN_ARCH_DIR)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
        set(_ASCCOMM_CANN_ARCH "aarch64-linux")
    else()
        set(_ASCCOMM_CANN_ARCH "x86_64-linux")
    endif()
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}")
endif()
message(STATUS "ASCCOMM_CANN_ARCH_DIR = ${ASCCOMM_CANN_ARCH_DIR}")

# 查找控制面源码：优先使用显式路径，
# 最后探测相邻源码仓；没有契约头时回退到随仓自包含头文件。
if(NOT ASCCOMM_CONTROL_PLANE_SOURCE_DIR)
    foreach(_candidate
        "${ASCCOMM_ROOT}/../hcomm"
        "${ASCCOMM_ROOT}/../../hcomm")
        if(EXISTS "${_candidate}/pkg_inc/hcomm/hcomm_res_entity_defs.h")
            get_filename_component(ASCCOMM_CONTROL_PLANE_SOURCE_DIR "${_candidate}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()
if(ASCCOMM_CONTROL_PLANE_SOURCE_DIR AND NOT EXISTS "${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/pkg_inc/hcomm/hcomm_res_entity_defs.h")
    # 指定路径无AICPU契约头(如devkit的cc包)：退回自包含。
    set(ASCCOMM_CONTROL_PLANE_SOURCE_DIR "")
endif()
set(ASCCOMM_CONTROL_PLANE_SOURCE_DIR "${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}" CACHE PATH "Original control-plane source tree")
message(STATUS "ASCCOMM_CONTROL_PLANE_SOURCE_DIR = ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}")

add_library(asc_comm_dfx INTERFACE)
target_include_directories(asc_comm_dfx INTERFACE ${ASCCOMM_DFX_SRC_DIR})

add_library(asccomm_aicpu SHARED)
add_subdirectory(asccomm)
if(TARGET runtime_headers)
    target_include_directories(asccomm_aicpu PRIVATE
        $<TARGET_PROPERTY:runtime_headers,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

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
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/profiling
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/aicpu_sched
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/aicpu
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/profiling
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/aicpu_sched
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/aicpu
    ${ASCCOMM_CANN_ARCH_DIR}/include/experiment/slog
    ${ASCCOMM_CANN_ARCH_DIR}/include/experiment/runtime
    ${ASCCOMM_ROOT}/include/aicpu_ts
    ${ASCCOMM_ROOT}/include/aicpu_ts/asccomm
    ${ASCCOMM_ROOT}/pkg_inc/hcomm
    # CANN包公共依赖头(securec.h/dlog_pub.h/ascend_hal.h/acl头)，
    # 独立构建不依赖cann-cmake导入目标，直接取CANN包目录
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/profiling
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/hcomm
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/include/hcomm
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/driver
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/profiling
    ${ASCCOMM_AICPU_ROOT}/asccomm/primitives/api_c_adpt
    ${ASCCOMM_AICPU_ROOT}/asccomm/primitives/launch
    ${ASCCOMM_AICPU_ROOT}/asccomm/common/exception
    ${ASCCOMM_AICPU_ROOT}/asccomm/common/utils
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/buffer
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/common
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/connection
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/stream
)
# Runtime source builds may not have installed pkg_inc headers yet. Consume
# public header targets below and support source layouts without those targets.
# Do not link profapi/runtime implementations: symbols are resolved at load time.
foreach(_runtime_root
    "${RUNTIME_DIR}"
    "${RUNTIME_SOURCE_DIR}"
    "${ASCCOMM_ROOT}/../runtime"
    "${ASCCOMM_ROOT}/../../runtime")
    if(EXISTS "${_runtime_root}/pkg_inc/profiling/prof_common.h")
        target_include_directories(asccomm_aicpu PRIVATE
            "${_runtime_root}/pkg_inc/profiling"
            "${_runtime_root}/pkg_inc/aicpu_sched"
            "${_runtime_root}/include/dfx/base")
        break()
    endif()
endforeach()

# 控制面源码可用时附加其pkg_inc包含路径(可选依赖，自包含构建时跳过)
if(ASCCOMM_CONTROL_PLANE_SOURCE_DIR)
    target_include_directories(asccomm_aicpu PRIVATE
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/include
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/pkg_inc
        ${ASCCOMM_CONTROL_PLANE_SOURCE_DIR}/pkg_inc/hcomm)
endif()

# 设备侧库：securec/dlog/驱动符号由设备运行环境(ccl_kernel/内核驱动)在加载期解析，
# 链接期不强制落地；cann-cmake导入目标存在时按需附加，避免独立构建缺目标报错
set(_asccomm_aicpu_link_targets asc_comm_dfx)
foreach(_target runtime_headers msprof_headers acl_rt_headers slog_headers ascend_hal c_sec)
    if(TARGET ${_target})
        list(APPEND _asccomm_aicpu_link_targets ${_target})
    endif()
endforeach()
if(TARGET runtime_headers)
    list(APPEND _asccomm_aicpu_link_targets runtime_headers)
endif()
target_link_libraries(asccomm_aicpu PRIVATE
    ${_asccomm_aicpu_link_targets}
    dl
    pthread
)

target_link_options(asccomm_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
)

set_target_properties(asccomm_aicpu PROPERTIES SKIP_BUILD_RPATH TRUE)

# Keep an explicit aggregate target, matching the CCU CMake integration, so
# superbuilds can depend on the AICPU data-plane project without knowing its
# internal source layout.
add_custom_target(asccomm_aicpu_compile_check DEPENDS asccomm_aicpu)
