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

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
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
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
)
target_include_directories(asccomm_ccu_dataplane_obj SYSTEM PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/runtime/include
    ${ASCCOMM_CANN_ARCH_DIR}/include
    ${ASCCOMM_CANN_ARCH_DIR}/pkg_inc
)
target_compile_definitions(asccomm_ccu_dataplane_obj PRIVATE
    ASCCOMM_CCU_LOCAL_LOGGING
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
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
target_link_libraries(asccomm_ccu_dataplane PRIVATE
    asccomm_ccu_headers
    acl_rt
    runtime
    unified_dlog
    c_sec
)

add_custom_target(asccomm_ccu_hcomm_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu_dataplane)
