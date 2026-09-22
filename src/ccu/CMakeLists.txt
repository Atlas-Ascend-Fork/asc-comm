# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_CCU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_CCU_ROOT}/../.. ABSOLUTE)

function(asccomm_collect_header_dirs out_var)
    set(_dirs)
    foreach(_root IN LISTS ARGN)
        if(EXISTS "${_root}")
            file(GLOB_RECURSE _headers CONFIGURE_DEPENDS "${_root}/*.h" "${_root}/*.hpp")
            foreach(_header IN LISTS _headers)
                get_filename_component(_dir "${_header}" DIRECTORY)
                list(APPEND _dirs "${_dir}")
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dirs)
    set(${out_var} ${_dirs} PARENT_SCOPE)
endfunction()

if(NOT ASCCOMM_CANN_ARCH_DIR AND ASCEND_CANN_PACKAGE_PATH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
        set(_ASCCOMM_CANN_ARCH "aarch64-linux")
    else()
        set(_ASCCOMM_CANN_ARCH "x86_64-linux")
    endif()
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${_ASCCOMM_CANN_ARCH}")
endif()

set(ASCCOMM_CANN_BASE_INCLUDE_DIRS
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/runtime/include
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc
)
asccomm_collect_header_dirs(ASCCOMM_CANN_HEADER_DIRS ${ASCCOMM_CANN_BASE_INCLUDE_DIRS})

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
)

add_library(asccomm_ccu_dataplane_obj OBJECT)
target_link_libraries(asccomm_ccu_dataplane_obj PRIVATE
    asccomm_ccu_headers
    $<BUILD_INTERFACE:intf_pub>
    $<BUILD_INTERFACE:error_manager_headers>
    $<BUILD_INTERFACE:acl_rt_headers>
    $<BUILD_INTERFACE:asc_host_headers>
    $<BUILD_INTERFACE:ascend_hal_headers>
    $<BUILD_INTERFACE:kernel_tiling_headers>
    $<BUILD_INTERFACE:atrace_headers>
    $<BUILD_INTERFACE:mmpa_headers>
    $<BUILD_INTERFACE:runtime_headers>
    $<BUILD_INTERFACE:rdma_core_headers>
    $<BUILD_INTERFACE:c_sec_headers>
    $<BUILD_INTERFACE:unified_dlog>
)
target_include_directories(asccomm_ccu_dataplane_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}
    ${ASCCOMM_ROOT}/include
)
target_include_directories(asccomm_ccu_dataplane_obj SYSTEM PRIVATE
    ${ASCCOMM_CANN_BASE_INCLUDE_DIRS}
    ${ASCCOMM_CANN_HEADER_DIRS}
)
target_compile_definitions(asccomm_ccu_dataplane_obj PRIVATE
    ASCCOMM_CCU_LOCAL_LOGGING
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
    _GLIBCXX_USE_CXX11_ABI=0
)
target_compile_options(asccomm_ccu_dataplane_obj PRIVATE -w -fPIC)
target_compile_options(asccomm_ccu_dataplane_obj PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${ASCCOMM_CCU_ROOT}/hcomm/common/ccu_log.h>
)
add_subdirectory(hcomm)

add_library(asccomm_ccu_dataplane SHARED
    $<TARGET_OBJECTS:asccomm_ccu_dataplane_obj>
)
set_target_properties(asccomm_ccu_dataplane PROPERTIES
    OUTPUT_NAME asccomm_ccu_dataplane
)
target_link_directories(asccomm_ccu_dataplane PRIVATE
    ${ASCCOMM_CANN_ARCH_DIR}/lib64
)
target_link_libraries(asccomm_ccu_dataplane PRIVATE
    asccomm_ccu_headers
    acl_rt
    runtime
    unified_dlog
    c_sec
    pthread
)
# 链接期校验：数据面 SO 不允许存在未定义符号（防止无意识依赖 hcomm 内部符号）
target_link_options(asccomm_ccu_dataplane PRIVATE -Wl,--no-undefined)

# Keep the mainline host-launch wrapper as a separate compatibility library.
# It uses the legacy HcommCcuKernel* registration ABI and must not be folded
# into the data-plane SO, whose cross-SO interface is the POD-based API above.
add_library(asccomm_ccu_host_obj OBJECT
    ${ASCCOMM_CCU_ROOT}/ccu_host_launch.cpp
)
target_link_libraries(asccomm_ccu_host_obj PRIVATE
    asccomm_ccu_headers
    $<$<TARGET_EXISTS:intf_pub>:intf_pub>
    $<$<TARGET_EXISTS:error_manager_headers>:error_manager_headers>
    $<$<TARGET_EXISTS:acl_rt_headers>:acl_rt_headers>
    $<$<TARGET_EXISTS:asc_host_headers>:asc_host_headers>
    $<$<TARGET_EXISTS:ascend_hal_headers>:ascend_hal_headers>
    $<$<TARGET_EXISTS:kernel_tiling_headers>:kernel_tiling_headers>
    $<$<TARGET_EXISTS:atrace_headers>:atrace_headers>
    $<$<TARGET_EXISTS:mmpa_headers>:mmpa_headers>
    $<$<TARGET_EXISTS:runtime_headers>:runtime_headers>
    $<$<TARGET_EXISTS:rdma_core_headers>:rdma_core_headers>
    $<$<TARGET_EXISTS:hccl_legacy_headers>:hccl_legacy_headers>
    $<$<TARGET_EXISTS:json>:json>
    $<$<TARGET_EXISTS:c_sec_headers>:c_sec_headers>
    $<$<TARGET_EXISTS:unified_dlog>:unified_dlog>
)
target_include_directories(asccomm_ccu_host_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}
)
target_include_directories(asccomm_ccu_host_obj SYSTEM PRIVATE
    ${ASCCOMM_CANN_BASE_INCLUDE_DIRS}
    ${ASCCOMM_CANN_HEADER_DIRS}
)
target_compile_definitions(asccomm_ccu_host_obj PRIVATE
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
)
target_compile_features(asccomm_ccu_host_obj PUBLIC cxx_std_17)
target_compile_options(asccomm_ccu_host_obj PRIVATE
    -fPIC
    -Wall
    -Werror
)

add_library(asccomm_ccu SHARED
    $<TARGET_OBJECTS:asccomm_ccu_host_obj>
)
if(NOT ASCCOMM_LIBRARY_OUTPUT_DIRECTORY)
    set(ASCCOMM_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
set_target_properties(asccomm_ccu PROPERTIES
    OUTPUT_NAME asccomm_ccu
    LIBRARY_OUTPUT_DIRECTORY ${ASCCOMM_LIBRARY_OUTPUT_DIRECTORY}
)
target_link_libraries(asccomm_ccu PRIVATE asccomm_ccu_headers)

if(NOT INSTALL_LIBRARY_DIR)
    set(INSTALL_LIBRARY_DIR lib64)
endif()
if(NOT INSTALL_PKG_INCLUDE_DIR)
    set(INSTALL_PKG_INCLUDE_DIR pkg_inc)
endif()

install(TARGETS asccomm_ccu
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
    COMPONENT hcomm)
install(FILES ${ASCCOMM_ROOT}/include/ccu/ccu_host_launch.h
    DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/ccu
    COMPONENT hcomm)

# 源文件归属校验：强制数据面目标的所有源文件必须位于 asc-comm 仓内，
# 避免误引用 hcomm 源（控制面/数据面边界），保证两仓可独立构建。
get_target_property(_ASCCOMM_CCU_SOURCES asccomm_ccu_dataplane_obj SOURCES)
get_target_property(_ASCCOMM_CCU_SOURCE_DIR asccomm_ccu_dataplane_obj SOURCE_DIR)
get_filename_component(_ASCCOMM_ROOT_REAL "${ASCCOMM_ROOT}" REALPATH)
foreach(_source IN LISTS _ASCCOMM_CCU_SOURCES)
    # 生成器表达式无法在配置期解析为真实路径，直接拒绝
    if(_source MATCHES "^\\$<")
        message(FATAL_ERROR "asccomm_ccu_dataplane_obj source ownership cannot be verified: ${_source}")
    endif()
    if(IS_ABSOLUTE "${_source}")
        set(_source_path "${_source}")
    else()
        set(_source_path "${_ASCCOMM_CCU_SOURCE_DIR}/${_source}")
    endif()
    get_filename_component(_source_real "${_source_path}" REALPATH)
    string(FIND "${_source_real}" "${_ASCCOMM_ROOT_REAL}/" _source_prefix)
    # 解析后的真实路径必须位于 asc-comm 根目录下，否则视为越界源文件
    if(NOT _source_prefix EQUAL 0)
        message(FATAL_ERROR "asccomm_ccu_dataplane_obj source is outside asc-comm: ${_source_real}")
    endif()
endforeach()

add_custom_target(asccomm_ccu_hcomm_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu asccomm_ccu_dataplane)
