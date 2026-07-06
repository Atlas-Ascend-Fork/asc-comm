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
asccomm_collect_header_dirs(ASCCOMM_HCCL_HEADER_DIRS
    ${ASCCOMM_HCCL_SOURCE_DIR}/src
    ${ASCCOMM_HCCL_SOURCE_DIR}/include
)
asccomm_collect_header_dirs(ASCCOMM_LOCAL_HCCL_HEADER_DIRS
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/common
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
)

add_library(asccomm_hcomm_obj OBJECT)
add_library(asccomm_ccu_hcomm ALIAS asccomm_hcomm_obj)
target_link_libraries(asccomm_hcomm_obj PRIVATE asccomm_ccu_headers)
target_include_directories(asccomm_hcomm_obj PRIVATE
    ${ASCCOMM_ROOT}/include
    ${ASCCOMM_ROOT}/include/ccu
    ${ASCCOMM_ROOT}/include/ccu/hcomm
    ${ASCCOMM_ROOT}/include/ccu/hcomm/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
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
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/ccu/ccu_transport
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/endpoint_pairs/channels/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/hccp/external_depends/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/hccp/rdma_service/ctx
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/utils
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/common/exception
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend950/unified_platform/pub_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/hcom
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/legacy/ascend910/framework/common/src/exception
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/endpoint_pairs/channels
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/common
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/southbound_adpt
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/external_depends
    ${ASCCOMM_HCOMM_SOURCE_DIR}/src/base_comm/resources/hccp/rdma_service/ctx
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/test/ut/platform/hccp/rdma_service_normal/include
    ${ASCCOMM_HCOMM_HEADER_DIRS}
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/acl
    ${CANN_3RD_LIB_PATH}/json/include
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc/profiling
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/include/driver
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc/driver
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc/runtime
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/base
)

add_subdirectory(dataplane/hcomm/base_comm/primitives/api_c_adpt/ccu)
add_subdirectory(dataplane/hcomm/base_comm/resources/ccu)
add_subdirectory(dataplane/hcomm/base_comm/resources/endpoint_pairs/channels/ccu)
target_sources(asccomm_hcomm_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/hccp/rdma_service/ctx/dl_ccu_function.c
    ${ASCCOMM_CCU_ROOT}/dataplane/hcomm/base_comm/resources/hccp/rdma_service/ctx/rs_ccu.c
)

add_library(asccomm_hccl_obj OBJECT)
add_library(asccomm_ccu_hccl ALIAS asccomm_hccl_obj)
target_link_libraries(asccomm_hccl_obj PRIVATE asccomm_ccu_headers)
target_compile_definitions(asccomm_hccl_obj PRIVATE
    BUILD_OPEN_PROJECT
    PRODUCT_SIDE=\"host\"
    CANN_VERSION_NUM=90100000
)
target_compile_options(asccomm_hccl_obj PRIVATE -w -fPIC)
target_include_directories(asccomm_hccl_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/common
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/common/hcomm_dlsym
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/common/hcomm_dlsym/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/op_common/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/op_common/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather_v/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_reduce/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_to_all_v/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/broadcast/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter_v/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/scatter/template
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather_v/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_gather_v/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_reduce/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_reduce/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_to_all_v/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/all_to_all_v/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/broadcast/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/broadcast/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter_v/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/reduce_scatter_v/template/ccu/kernel
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/scatter/template/ccu
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/scatter/template/ccu/kernel
    ${ASCCOMM_HCCL_SOURCE_DIR}/src
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/common
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/common/hcomm_dlsym
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops/op_common
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops/op_common/executor/channel
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops/op_common/inc
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops/op_common/template
    ${ASCCOMM_HCCL_SOURCE_DIR}/src/ops/op_common/template/wrapper
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/ccu
    ${ASCCOMM_HCOMM_SOURCE_DIR}/include/hccl
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc
    ${ASCCOMM_HCOMM_SOURCE_DIR}/pkg_inc/hccl
    ${ASCCOMM_HCCL_HEADER_DIRS}
    ${ASCCOMM_LOCAL_HCCL_HEADER_DIRS}
    ${ASCCOMM_HCOMM_HEADER_DIRS}
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/include/driver
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc/driver
    ${ASCEND_CANN_PACKAGE_PATH}/aarch64-linux/pkg_inc/runtime
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/base
)

set(PRODUCT_SIDE host)
set(STATIC_MODE ON)
add_subdirectory(dataplane/hccl/common/hcomm_dlsym)
add_subdirectory(dataplane/hccl/ops/op_common/template/ccu)
target_sources(asccomm_hccl_obj PRIVATE
    ${ASCCOMM_CCU_ROOT}/dataplane/hccl/ops/op_common/template/ccu_alg_template_base.cc
)
add_subdirectory(dataplane/hccl/ops/all_gather/template/ccu)
add_subdirectory(dataplane/hccl/ops/all_gather_v/template/ccu)
add_subdirectory(dataplane/hccl/ops/all_reduce/template/ccu)
add_subdirectory(dataplane/hccl/ops/all_to_all_v/template/ccu)
add_subdirectory(dataplane/hccl/ops/broadcast/template/ccu)
add_subdirectory(dataplane/hccl/ops/reduce/template/ccu)
add_subdirectory(dataplane/hccl/ops/reduce_scatter/template/ccu)
add_subdirectory(dataplane/hccl/ops/reduce_scatter_v/template/ccu)
add_subdirectory(dataplane/hccl/ops/scatter/template/ccu)

add_library(asccomm_ccu_dataplane SHARED
    $<TARGET_OBJECTS:asccomm_hcomm_obj>
    $<TARGET_OBJECTS:asccomm_hccl_obj>
)
set_target_properties(asccomm_ccu_dataplane PROPERTIES
    OUTPUT_NAME asccomm_ccu_dataplane
)
target_link_libraries(asccomm_ccu_dataplane PRIVATE asccomm_ccu_headers)

add_library(asccomm_ccu_device_headers INTERFACE)
target_link_libraries(asccomm_ccu_device_headers INTERFACE asccomm_ccu_headers)
target_include_directories(asccomm_ccu_device_headers INTERFACE
    ${ASCCOMM_CCU_ROOT}/dataplane/device/impl/adv_api/detail
    ${ASCCOMM_CCU_ROOT}/dataplane/device/impl/adv_api/detail/hccl
    ${ASCCOMM_CCU_ROOT}/dataplane/device/impl/adv_api/detail/hccl/impl
    ${ASCCOMM_ASCDEVKIT_SOURCE_DIR}/include
    ${ASCCOMM_ASCDEVKIT_SOURCE_DIR}/include/adv_api
    ${ASCCOMM_ASCDEVKIT_SOURCE_DIR}/impl
    ${ASCCOMM_ASCDEVKIT_SOURCE_DIR}/impl/adv_api/detail
)

add_custom_target(asccomm_ccu_hcomm_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_hccl_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_dataplane_compile_check DEPENDS asccomm_ccu_dataplane)
add_custom_target(asccomm_ccu_compile_check DEPENDS
    asccomm_ccu_hcomm_dataplane_compile_check
    asccomm_ccu_hccl_compile_check
)
