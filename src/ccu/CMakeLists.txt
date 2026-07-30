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

if(NOT ASCEND_CANN_PACKAGE_PATH)
    if(DEFINED ENV{ASCEND_HOME_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}")
    elseif(DEFINED ENV{ASCEND_CANN_PACKAGE_PATH})
        set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_CANN_PACKAGE_PATH}")
    endif()
endif()

if(NOT ASCCOMM_CANN_ARCH_DIR OR NOT EXISTS "${ASCCOMM_CANN_ARCH_DIR}")
    foreach(_candidate
        "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux"
        "${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux"
        "${ASCEND_CANN_PACKAGE_PATH}/x86_64-linux"
        "${ASCEND_CANN_PACKAGE_PATH}")
        if(EXISTS "${_candidate}")
            set(ASCCOMM_CANN_ARCH_DIR "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT ASCCOMM_CANN_ARCH_DIR)
    set(ASCCOMM_CANN_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux")
endif()

function(asccomm_collect_header_dirs out_var)
    set(_dirs)
    foreach(_root IN LISTS ARGN)
        if(EXISTS "${_root}")
            file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
                "${_root}/*.h"
                "${_root}/*.hpp"
            )
            foreach(_header IN LISTS _headers)
                get_filename_component(_dir "${_header}" DIRECTORY)
                list(APPEND _dirs "${_dir}")
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dirs)
    set(${out_var} ${_dirs} PARENT_SCOPE)
endfunction()

set(ASCCOMM_HCOMM_HEADER_DIRS)
if(ASCCOMM_HCOMM_SOURCE_DIR AND EXISTS "${ASCCOMM_HCOMM_SOURCE_DIR}")
    asccomm_collect_header_dirs(ASCCOMM_HCOMM_HEADER_DIRS
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src
        ${ASCCOMM_HCOMM_SOURCE_DIR}/include
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    )
endif()
set(ASCCOMM_CANN_BASE_INCLUDE_DIRS)
if(ASCEND_CANN_PACKAGE_PATH)
    list(APPEND ASCCOMM_CANN_BASE_INCLUDE_DIRS
        ${ASCEND_CANN_PACKAGE_PATH}/include
        ${ASCEND_CANN_PACKAGE_PATH}/runtime/include
    )
endif()
if(ASCCOMM_CANN_ARCH_DIR)
    list(APPEND ASCCOMM_CANN_BASE_INCLUDE_DIRS
        ${ASCCOMM_CANN_ARCH_DIR}/include
        ${ASCCOMM_CANN_ARCH_DIR}/include/hcomm/ccu
        ${ASCCOMM_CANN_ARCH_DIR}/include/hccl
        ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc
    )
endif()
asccomm_collect_header_dirs(ASCCOMM_CANN_HEADER_DIRS
    ${ASCCOMM_CANN_BASE_INCLUDE_DIRS}
)

set(ASCCOMM_HCOMM_PUBLIC_INCLUDE_DIRS)
set(ASCCOMM_HCOMM_PRIVATE_INCLUDE_DIRS)
if(ASCCOMM_HCOMM_SOURCE_DIR AND EXISTS "${ASCCOMM_HCOMM_SOURCE_DIR}")
    set(ASCCOMM_HCOMM_PUBLIC_INCLUDE_DIRS
        ${ASCCOMM_HCOMM_SOURCE_DIR}/include
        ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hcomm/ccu
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy/hccl
        ${ASCCOMM_HCOMM_SOURCE_DIR}/third_party/json/include
        ${ASCCOMM_HCOMM_SOURCE_DIR}/third_party/json/single_include
    )
    set(ASCCOMM_HCOMM_PRIVATE_INCLUDE_DIRS
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/common
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/common/src
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/common/src/mgr
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/utils
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/exception
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform/pub_inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/hcom
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/common/src/exception
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc/inner
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/pub_inc/new
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/endpoint_pairs/channels
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/common
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/southbound_adpt
        ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/external_depends
        ${ASCCOMM_HCOMM_SOURCE_DIR}/include
        ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hccl
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy
        ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy/hccl
        ${ASCCOMM_HCOMM_SOURCE_DIR}/test/ut/platform/hccp/rdma_service_normal/include
        ${ASCCOMM_HCOMM_HEADER_DIRS}
    )
endif()

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_HCOMM_PUBLIC_INCLUDE_DIRS}
)

add_library(asccomm_ccu_obj OBJECT
    ${ASCCOMM_CCU_ROOT}/ccu_host_launch.cpp
)
target_link_libraries(asccomm_ccu_obj PRIVATE
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
target_include_directories(asccomm_ccu_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}
    ${ASCCOMM_HCOMM_PRIVATE_INCLUDE_DIRS}
)
target_include_directories(asccomm_ccu_obj SYSTEM PRIVATE
    ${ASCCOMM_CANN_BASE_INCLUDE_DIRS}
    ${ASCCOMM_CANN_HEADER_DIRS}
)
target_compile_definitions(asccomm_ccu_obj PRIVATE
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
)
target_compile_features(asccomm_ccu_obj PUBLIC cxx_std_17)
target_compile_options(asccomm_ccu_obj PRIVATE
    -fPIC
    -Wall
    -Werror
)

add_library(asccomm_ccu SHARED
    $<TARGET_OBJECTS:asccomm_ccu_obj>
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
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR} ${INSTALL_OPTIONAL}
    COMPONENT hcomm)

if(EXISTS "${ASCCOMM_ROOT}/include/ccu")
    install(DIRECTORY ${ASCCOMM_ROOT}/include/ccu/
        DESTINATION ${INSTALL_PKG_INCLUDE_DIR}/ccu
        COMPONENT hcomm
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
endif()

add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu)
