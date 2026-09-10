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

# hcomm源码定位(可选依赖)：AICPU数据面自包含随仓契约头(include/aicpu/hcomm/)编译，
# hcomm源码存在时附加其pkg_inc包含路径(兼容旧依赖链)，不存在时自包含构建。
# 显式指定(HCOMM_SOURCE_DIR/ASCCOMM_HCOMM_SOURCE_DIR)但目录无效时报错(防拼写错误)；
# 未指定且同级目录探测失败仅告警并自包含——第三方超构建(如CANN cann_device)的
# asc-comm集成不携带hcomm源码路径，依赖此前"探测失败被容忍"的行为。
set(_asccomm_hcomm_explicit FALSE)
if(ASCCOMM_HCOMM_SOURCE_DIR)
    set(_asccomm_hcomm_explicit TRUE)
elseif(HCOMM_SOURCE_DIR)
    set(ASCCOMM_HCOMM_SOURCE_DIR "${HCOMM_SOURCE_DIR}")
    set(_asccomm_hcomm_explicit TRUE)
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
if(ASCCOMM_HCOMM_SOURCE_DIR AND NOT EXISTS "${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hcomm/hcomm_pod_resource.h")
    if(_asccomm_hcomm_explicit)
        message(FATAL_ERROR
            "HCOMM_SOURCE_DIR(${ASCCOMM_HCOMM_SOURCE_DIR}) has no pkg_inc/hcomm/hcomm_pod_resource.h; "
            "fix the path or unset it to build the AICPU data plane self-contained.")
    endif()
    # 探测命中但无效(非源码根)：退回自包含
    set(ASCCOMM_HCOMM_SOURCE_DIR "")
endif()
if(ASCCOMM_HCOMM_SOURCE_DIR)
    set(ASCCOMM_HCOMM_SOURCE_DIR "${ASCCOMM_HCOMM_SOURCE_DIR}" CACHE PATH "Original hcomm source tree")
    message(STATUS "ASCCOMM_HCOMM_SOURCE_DIR = ${ASCCOMM_HCOMM_SOURCE_DIR}")
else()
    message(STATUS "ASCCOMM_HCOMM_SOURCE_DIR not set; "
        "build AICPU data plane self-contained from in-repo contract headers")
endif()

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
# hcomm源码可用时附加其pkg_inc包含路径(可选依赖，自包含构建时跳过)
if(ASCCOMM_HCOMM_SOURCE_DIR)
    target_include_directories(asccomm_aicpu PRIVATE
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hcomm)
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

# 出包参照 src/ccu：库安装至 lib64、契约头安装至 pkg_inc/asccomm(数据面SO与hcomm控制面
# 通过POD实体解耦，交付物=库+契约头，消费者从安装目录引用而非源码目录耦合)
if(NOT INSTALL_LIBRARY_DIR)
    set(INSTALL_LIBRARY_DIR lib64)
endif()
if(NOT INSTALL_PKG_INCLUDE_DIR)
    set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
endif()
install(TARGETS asccomm_aicpu
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
    COMPONENT hcomm)
install(DIRECTORY ${ASCCOMM_ROOT}/include/aicpu/hcomm/
    DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/asccomm
    COMPONENT hcomm
    FILES_MATCHING PATTERN "*.h")
