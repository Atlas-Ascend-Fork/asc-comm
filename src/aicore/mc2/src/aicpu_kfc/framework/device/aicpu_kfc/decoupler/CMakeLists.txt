# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set(src_list
    ${CMAKE_CURRENT_SOURCE_DIR}/comm_kfc_aicpu_server.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/comm_kfc_dispatcher.cc
)

set(OPEN_HCCL_SRC_ROOT
    ${HCCL_CC_DIR}/src
)

set(open_src_list
    ${CMAKE_CURRENT_SOURCE_DIR}/comm_kfc_open_kernel_adapter.cc
)

set(open_exec_src_list
    ${CMAKE_CURRENT_SOURCE_DIR}/comm_kfc_open_kernel_executor.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/cann_dispatch_bridge.cc
)

set(open_exec_impl_src_list
    ${CMAKE_CURRENT_SOURCE_DIR}/comm_kfc_open_env_adapter.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel/channel.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel/channel_request.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/registry/coll_alg_v2_exec_registry.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/executor_base.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/executor_v2_base.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/alg_template_base.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/alg_v2_template_base.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/template_utils.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/registry/alg_v2_template_register.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/wrapper/alg_data_trans_wrapper.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/wrapper/dpu_alg_data_trans_wrapper.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo/topo.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo/topo_match_1d.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo/topo_match_base.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo/topo_match_multilevel.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo/topo_match_ubx.cc

    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/executor/ins_v2_reduce_scatter_sole_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/executor/ins_reduce_scatter_parallel_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/executor/ins_v2_reduce_scatter_sequence_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu/ins_temp_reduce_scatter_mesh_1D.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu/ins_temp_reduce_scatter_nhr.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu/ins_temp_reduce_scatter_mesh_1D_meshchunk.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu/ins_temp_reduce_scatter_mesh_1d_dpu.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu/ins_temp_reduce_scatter_aicpu_reduce_nhr.cc

    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/executor/ins_v2_all_gather_sole_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/executor/ins_v2_all_gather_parallel_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/executor/ins_v2_all_gather_sequence_executor.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template/aicpu/ins_temp_all_gather_mesh_1D.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template/aicpu/ins_temp_all_gather_nhr.cc
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template/aicpu/ins_temp_all_gather_nhr_dpu.cc
)

add_library(ccl_kernel_decoupler OBJECT ${src_list})
set_target_properties(ccl_kernel_decoupler PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(ccl_kernel_decoupler PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
)

target_compile_options(ccl_kernel_decoupler PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
)

target_include_directories(ccl_kernel_decoupler PRIVATE
    ${HCCL_HCOMM_HEADER_DIR}/../
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hcomm/ccu
    ${HCCL_BASE_DIR}/legacy/unified_platform/resource/stream/aicpu

    $<TARGET_PROPERTY:asc_ccl_kernel,INCLUDE_DIRECTORIES>
)

add_library(ccl_kernel_decoupler_open OBJECT ${open_src_list})
set_target_properties(ccl_kernel_decoupler_open PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(ccl_kernel_decoupler_open PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
)

target_compile_options(ccl_kernel_decoupler_open PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
)

target_include_directories(ccl_kernel_decoupler_open BEFORE PRIVATE
    ${HCCL_HCOMM_HEADER_DIR}/../
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/common
    ${OPEN_HCCL_SRC_ROOT}/common/hcomm_dlsym
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hcomm/ccu

    $<TARGET_PROPERTY:asc_ccl_kernel,INCLUDE_DIRECTORIES>
)

target_link_libraries(ccl_kernel_decoupler_open PRIVATE
    $<BUILD_INTERFACE:acl_rt_headers>
)

add_library(ccl_kernel_decoupler_open_exec OBJECT ${open_exec_src_list})
set_target_properties(ccl_kernel_decoupler_open_exec PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(ccl_kernel_decoupler_open_exec PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
    AICPU_COMPILE
)

target_compile_options(ccl_kernel_decoupler_open_exec PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
)

target_include_directories(ccl_kernel_decoupler_open_exec BEFORE PRIVATE
    ${HCCL_LOCAL_HEADER_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HCCL_HCOMM_HEADER_DIR}/../
    ${HCCL_HCOMM_HEADER_DIR}
    ${HCCL_HCOMM_HEADER_DIR}/hccl
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/wrapper
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/registry
    ${OPEN_HCCL_SRC_ROOT}/common
    ${OPEN_HCCL_SRC_ROOT}/common/hcomm_dlsym

    ${HCCL_BASE_DIR}/pub_inc
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hccl
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hcomm/ccu
    ${HCCL_BASE_DIR}/platform/hccp/external_depends/inc/log
)

target_link_libraries(ccl_kernel_decoupler_open_exec PRIVATE c_sec unified_dlog acl_rt_headers)

add_library(ccl_kernel_decoupler_open_exec_impl OBJECT ${open_exec_impl_src_list})
set_target_properties(ccl_kernel_decoupler_open_exec_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(ccl_kernel_decoupler_open_exec_impl PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
    AICPU_COMPILE
)

target_compile_options(ccl_kernel_decoupler_open_exec_impl PRIVATE
    $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-value
    -Wno-sign-compare
)

target_include_directories(ccl_kernel_decoupler_open_exec_impl BEFORE PRIVATE
    ${HCCL_LOCAL_HEADER_DIRS}
    ${HCCL_HCOMM_HEADER_DIR}/../
    ${HCCL_HCOMM_HEADER_DIR}
    ${HCCL_HCOMM_HEADER_DIR}/hccl
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/wrapper
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/topo
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/channel
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/executor/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/registry
    ${OPEN_HCCL_SRC_ROOT}/ops/op_common/template/wrapper
    ${OPEN_HCCL_SRC_ROOT}/common
    ${OPEN_HCCL_SRC_ROOT}/common/hcomm_dlsym
    ${OPEN_HCCL_SRC_ROOT}/hccl_public
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aicpu
    ${OPEN_HCCL_SRC_ROOT}/ops/reduce_scatter/template/aiv
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template/aicpu
    ${OPEN_HCCL_SRC_ROOT}/ops/all_gather/template/aiv
    ${OPEN_HCCL_SRC_ROOT}/ops/all_to_all_v/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/all_to_all_v/template
    ${OPEN_HCCL_SRC_ROOT}/ops/all_to_all_v/template/aicpu
    ${OPEN_HCCL_SRC_ROOT}/ops/all_to_all_v/template/aiv
    ${OPEN_HCCL_SRC_ROOT}/ops/all_reduce/executor
    ${OPEN_HCCL_SRC_ROOT}/ops/all_reduce/template
    ${OPEN_HCCL_SRC_ROOT}/ops/all_reduce/template/aicpu
    ${OPEN_HCCL_SRC_ROOT}/ops/all_reduce/template/aiv

    ${HCCL_BASE_DIR}/pub_inc
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hccl
    ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hcomm/ccu
    ${HCCL_BASE_DIR}/platform/hccp/external_depends/inc/log
)

target_sources(asc_ccl_kernel PRIVATE
    $<TARGET_OBJECTS:ccl_kernel_decoupler>
    $<TARGET_OBJECTS:ccl_kernel_decoupler_open>
    $<TARGET_OBJECTS:ccl_kernel_decoupler_open_exec>
    $<TARGET_OBJECTS:ccl_kernel_decoupler_open_exec_impl>
)

if(TARGET mc2_server)
    add_library(mc2_server_decoupler OBJECT ${src_list})
    set_target_properties(mc2_server_decoupler PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(mc2_server_decoupler PRIVATE
        $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_DEFINITIONS>
        MC2_SERVER_ONLY
    )

    target_compile_options(mc2_server_decoupler PRIVATE
        $<TARGET_PROPERTY:asc_ccl_kernel,COMPILE_OPTIONS>
    )

    target_include_directories(mc2_server_decoupler PRIVATE
        ${HCCL_HCOMM_HEADER_DIR}/../
        ${OPEN_HCCL_SRC_ROOT}/ops/op_common/inc
        ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hcomm/ccu
        ${HCCL_BASE_DIR}/legacy/unified_platform/resource/stream/aicpu

        $<TARGET_PROPERTY:asc_ccl_kernel,INCLUDE_DIRECTORIES>
    )

    target_link_libraries(mc2_server_decoupler PRIVATE
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:ascend_hal_headers>
        $<BUILD_INTERFACE:metadef_headers>
    )

    target_sources(mc2_server PRIVATE
        $<TARGET_OBJECTS:mc2_server_decoupler>
        $<TARGET_OBJECTS:ccl_kernel_decoupler_open>
        $<TARGET_OBJECTS:ccl_kernel_decoupler_open_exec>
    )
endif()
