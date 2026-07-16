# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

set(ASCCOMM_CCU_ROOT ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(ASCCOMM_ROOT ${ASCCOMM_CCU_ROOT}/../.. ABSOLUTE)

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

asccomm_collect_header_dirs(ASCCOMM_HCOMM_HEADER_DIRS
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
)

add_library(asccomm_ccu_headers INTERFACE)
target_include_directories(asccomm_ccu_headers INTERFACE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy/hccl
)

add_library(asccomm_hcomm_obj OBJECT)
add_library(asccomm_ccu_hcomm ALIAS asccomm_hcomm_obj)
target_link_libraries(asccomm_hcomm_obj PRIVATE
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
    $<BUILD_INTERFACE:hccl_legacy_headers>
    $<BUILD_INTERFACE:json>
    $<BUILD_INTERFACE:c_sec_headers>
    $<BUILD_INTERFACE:unified_dlog>
)
target_include_directories(asccomm_hcomm_obj PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/legacy/hccl
)
target_compile_definitions(asccomm_hcomm_obj PRIVATE
    BUILD_OPEN_PROJECT
    CANN_VERSION_NUM=90100000
)
target_compile_options(asccomm_hcomm_obj PRIVATE -w -fPIC)
target_compile_options(asccomm_hcomm_obj PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/ccu_dataplane_compat.h>
)
target_include_directories(asccomm_hcomm_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/primitives/api_c_adpt/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/pub_inc
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_device
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_device/ccu_comp
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_device/ccu_comp/ccu_channel
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_device/ccu_comp/ccu_channel/ccu_channel_ctx_v1
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_device/ccu_comp/ccu_channel/ccu_pfe
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_instance
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_microcode
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/context
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/interface
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/common
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/arithmetic
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/control
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/data
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/loop
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/sync
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_representation/reps/translator
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_transport
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/endpoint_pairs/channels/ccu
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

add_subdirectory(dataplane/hcomm/base_comm/primitives/api_c_adpt/ccu)
add_subdirectory(dataplane/hcomm/base_comm/resources/ccu)

add_library(asccomm_ccu_dataplane SHARED
    $<TARGET_OBJECTS:asccomm_hcomm_obj>
)
set_target_properties(asccomm_ccu_dataplane PROPERTIES
    OUTPUT_NAME asccomm_ccu_dataplane
)
target_link_libraries(asccomm_ccu_dataplane PRIVATE asccomm_ccu_headers)

add_custom_target(asccomm_ccu_hcomm_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS asccomm_ccu_dataplane)
