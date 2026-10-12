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
        "${ASCCOMM_ROOT}/../hcomm_pkg_inc"
        "${ASCCOMM_ROOT}/../hcomm")
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
    # CANN包公共依赖头(securec.h/dlog_pub.h/ascend_hal.h/acl头)，
    # 独立构建不依赖cann-cmake导入目标，直接取CANN包目录
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/hcomm
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/hcomm
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/include/hcomm
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/base
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc/driver
    ${ASCCOMM_AICPU_ROOT}/asccomm/primitives/api_c_adpt
    ${ASCCOMM_AICPU_ROOT}/asccomm/primitives/launch
    ${ASCCOMM_AICPU_ROOT}/asccomm/common/exception
    ${ASCCOMM_AICPU_ROOT}/asccomm/common/utils
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/buffer
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/common
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/connection
    ${ASCCOMM_AICPU_ROOT}/asccomm/resource/stream
)
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

target_link_options(asccomm_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,--gc-sections
)

set_target_properties(asccomm_aicpu PROPERTIES SKIP_BUILD_RPATH TRUE)

# install规则仅在asc-comm根工程(独立/自包含构建)下注册；
# 第三方超构建(runtime cann_device等)直接add_subdirectory(src/aicpu)时，
# 设备超构建的默认目标集可能不包含本目标(EXCLUDE_FROM_ALL或不被默认依赖拉入)，
# 但install规则会残留到宿主CPack，导致"file INSTALL cannot find"硬失败。
# 超构建场景由宿主自行install/打包(如runtime的DEVICE_LIBRARY_PATH)。
if("${PROJECT_SOURCE_DIR}" STREQUAL "${ASCCOMM_ROOT}")
    if(NOT INSTALL_LIBRARY_DIR)
        set(INSTALL_LIBRARY_DIR lib64)
    endif()
    if(NOT INSTALL_PKG_INCLUDE_DIR)
        set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
    endif()
    install(TARGETS asccomm_aicpu
        LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
        COMPONENT asccomm)
    install(DIRECTORY ${ASCCOMM_ROOT}/include/aicpu/hcomm/
        DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/asccomm
        COMPONENT asccomm
        FILES_MATCHING PATTERN "*.h")
endif()

# Keep an explicit aggregate target, matching the CCU CMake integration, so
# superbuilds can depend on the AICPU data-plane project without knowing its
# internal source layout.
add_custom_target(asccomm_aicpu_compile_check DEPENDS asccomm_aicpu)
