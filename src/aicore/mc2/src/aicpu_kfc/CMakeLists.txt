# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set(HCCL_HCOMM_HEADER_DIR ${HCCL_CC_INCLUDE_DIR}/hcomm)
set(HCCL_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ORION_REAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/legacy)
set(HCOMM_REAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/..)

#组件间对外头文件目录
add_library(asc_hccl_headers INTERFACE)
target_include_directories(asc_hccl_headers INTERFACE
    $<BUILD_INTERFACE:${TOP_DIR}/hcomm/include>
    $<BUILD_INTERFACE:${TOP_DIR}/hcomm/include/hccl>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/hccl>
    $<INSTALL_INTERFACE:include/hccl/external>
    $<INSTALL_INTERFACE:include/hccl/external/hccl>
)

# 创建 ccl_kernel_plf共享库
add_library(asc_ccl_kernel_plf  SHARED)
# 创建 ccl_kernel_plf_a静态库
add_library(asc_ccl_kernel_plf_a  STATIC)

# 需要编译的文件list
set(include_list
    ${HCCL_LOCAL_HEADER_DIRS}
    ${HCCL_BASE_DIR}/pub_inc
    ${HCCL_BASE_DIR}/pub_inc/aicpu/
    ${HCCL_BASE_DIR}/pub_inc/inner
    ${HCCL_BASE_DIR}/pub_inc/new

    ${HCCL_BASE_DIR}/algorithm/base/inc
    ${HCCL_BASE_DIR}/algorithm/pub_inc
    ${HCCL_BASE_DIR}/algorithm/base/alg_aiv_template
    ${HCCL_BASE_DIR}/algorithm/base/alg_template
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/component
    ${HCCL_BASE_DIR}/algorithm/base/mc2_handler
    ${HCCL_BASE_DIR}/algorithm/base/communicator
    ${HCCL_BASE_DIR}/algorithm/impl
    ${HCCL_BASE_DIR}/algorithm/impl/inc
    ${HCCL_BASE_DIR}/algorithm/impl/legacy
    ${HCCL_BASE_DIR}/algorithm/impl/legacy/operator
    ${HCCL_BASE_DIR}/algorithm/base/communicator/legacy
    ${HCCL_BASE_DIR}/algorithm/impl/resource_manager
    ${HCCL_BASE_DIR}/algorithm/impl/task
    ${HCCL_BASE_DIR}/algorithm/impl/operator
    ${HCCL_BASE_DIR}/algorithm/impl/operator/registry
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/registry
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_send_receive
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_reduce
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_reduce/310P
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_gather
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_gather/310P
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_gather_v
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_reduce_scatter
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_reduce_scatter/310P
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_reduce_scatter_v
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_reduce_scatter_v/310P
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_all_to_all
    ${HCCL_BASE_DIR}/algorithm/impl/coll_executor/coll_scatter
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_all_gather
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_all_reduce
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_alltoall
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_alltoallv
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_broadcast
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_reduce
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_reduce_scatter
    ${HCCL_BASE_DIR}/algorithm/base/alg_template/temp_scatter
    ${HCCL_BASE_DIR}/algorithm/base/alg_template
    ${HCCL_BASE_DIR}/algorithm/pub_inc

    ${HCCL_BASE_DIR}/common/health
    ${HCCL_BASE_DIR}/common/debug/profiling/inc
    ${HCCL_BASE_DIR}/common/debug/config/
    ${HCCL_BASE_DIR}/common/stream
    ${HCCL_BASE_DIR}/common/launch_device

    ${HCCL_BASE_DIR}/framework/inc
    ${HCCL_BASE_DIR}/framework/inc/hccd
    ${HCCL_BASE_DIR}/framework/cluster_maintenance/health/heartbeat/
    ${HCCL_BASE_DIR}/framework/cluster_maintenance/detect/detect_connect_anomalies/
    ${HCCL_BASE_DIR}/framework/cluster_maintenance/recovery/operator_retry
    ${HCCL_BASE_DIR}/framework/
    ${HCCL_BASE_DIR}/framework/common/src/
    ${HCCL_BASE_DIR}/framework/common/src/exception
    ${HCCL_BASE_DIR}/framework/common/src/config
    ${HCCL_BASE_DIR}/framework/common/src/task
    ${HCCL_BASE_DIR}/framework/common/src/topo
    ${HCCL_BASE_DIR}/framework/common/src/mgr
    ${HCCL_BASE_DIR}/framework/hcom
    ${HCCL_BASE_DIR}/framework/communicator/impl/
    ${HCCL_BASE_DIR}/framework/communicator/impl/one_sided_service/
    ${HCCL_BASE_DIR}/framework/communicator/impl/zero_copy
    ${HCCL_BASE_DIR}/framework/communicator/impl/resource_manager
    ${HCCL_BASE_DIR}/framework/communicator/impl/independent_op
    ${HCCL_BASE_DIR}/framework/communicator/impl/independent_op/rank_graph
    ${HCCL_BASE_DIR}/framework/communicator/impl/independent_op/resource/engine
    ${HCCL_BASE_DIR}/framework/communicator/impl/independent_op/channel
    ${HCCL_BASE_DIR}/framework/communicator/impl/independent_op/channel/device
    ${HCCL_BASE_DIR}/framework/next/comms/comm_engine_res/threads
    ${HCCL_BASE_DIR}/framework/next/coll_comms/rank
    ${HCCL_BASE_DIR}/framework/next/coll_comms/communicator
    ${HCCL_BASE_DIR}/framework/next/coll_comms/communicator/aicpu
    ${HCCL_BASE_DIR}/framework/op_base/src/

    ${HCCL_BASE_DIR}/platform/
    ${HCCL_BASE_DIR}/platform/inc
    ${HCCL_BASE_DIR}/platform/inc/adapter
    ${HCCL_BASE_DIR}/platform/inc/adapter/hccd
    ${HCCL_BASE_DIR}/platform/resource/dispatcher_ctx
    ${HCCL_BASE_DIR}/platform/common/
    ${HCCL_BASE_DIR}/platform/common/unique
    ${HCCL_BASE_DIR}/platform/common/buffer_manager
    ${HCCL_BASE_DIR}/platform/common/unfold_cache
    ${HCCL_BASE_DIR}/platform/resource/transport
    ${HCCL_BASE_DIR}/platform/resource/transport/device
    ${HCCL_BASE_DIR}/platform/resource/transport/host
    ${HCCL_BASE_DIR}/platform/resource/transport/heterog
    ${HCCL_BASE_DIR}/platform/resource/transport/onesided
    ${HCCL_BASE_DIR}/platform/resource/transport/onesided/device
    ${HCCL_BASE_DIR}/platform/resource/notify
    ${HCCL_BASE_DIR}/platform/ping_mesh
    ${HCCL_BASE_DIR}/platform/resource/socket
    ${HCCL_BASE_DIR}/platform/task
    ${HCCL_BASE_DIR}/platform/typical

    ${HCCL_BASE_DIR}/framework/next/coll_comms/rank_pairs
    ${HCCL_BASE_DIR}/legacy/unified_platform/resource/transport
    ${HCCL_BASE_DIR}/legacy/common/utils/
    ${HCCL_BASE_DIR}/legacy/common/exception/
    ${HCCL_BASE_DIR}/legacy/common/types/
    ${HCCL_BASE_DIR}/legacy/common/
    ${HCCL_BASE_DIR}/legacy/framework/resource_manager/socket/
    ${HCCL_BASE_DIR}/legacy/unified_platform/external_system/
    ${HCCL_BASE_DIR}/legacy/unified_platform/common/
    ${HCCL_BASE_DIR}/framework/next/comms/endpoint_pairs/sockets/
    ${HCCL_BASE_DIR}/framework/next/comms/endpoints/
    ${HCCL_BASE_DIR}/framework/next/comms/endpoint_pairs/
)

# 指定ccl_kernel_plf所需包含文件搜索路径
if(BUILD_OPEN_PROJECT)
    add_cann_third_party(json)
    add_cann_third_party(rdma-core)

    if(NOT TARGET rdma_core_headers)
        add_library(rdma_core_headers INTERFACE)

        target_include_directories(rdma_core_headers INTERFACE
            ${RDMA_CORE_INCLUDE_DIR}
        )
    endif()

    target_compile_definitions(asc_ccl_kernel_plf PRIVATE
        OPEN_BUILD_PROJECT)

    target_compile_definitions(asc_ccl_kernel_plf_a PRIVATE
        OPEN_BUILD_PROJECT)

    target_include_directories(asc_ccl_kernel_plf PRIVATE
        ${HCCL_HCOMM_HEADER_DIR}
        ${HCCL_HCOMM_HEADER_DIR}/hccl
        ${HCCL_HCOMM_HEADER_DIR}/pkg_inc
        ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hccl
        ${HCCL_BASE_DIR}/pub_inc
        ${HCCL_BASE_DIR}/pub_inc/aicpu
        ${HCCL_BASE_DIR}/pub_inc/inner
        ${HCCL_BASE_DIR}/pub_inc/new
        ${HCCL_BASE_DIR}/platform/hccp/inc
        ${HCCL_BASE_DIR}/platform/hccp/inc/network
        # 临时依赖头文件，待删除
        ${HCCL_HCOMM_HEADER_DIR}/externel_depends/tsch/
        ${THIRD_PARTY_NLOHMANN_PATH}
    )

    target_include_directories(asc_ccl_kernel_plf_a PRIVATE
        ${HCCL_HCOMM_HEADER_DIR}
        ${HCCL_HCOMM_HEADER_DIR}/hccl
        ${HCCL_HCOMM_HEADER_DIR}/pkg_inc
        ${HCCL_HCOMM_HEADER_DIR}/pkg_inc/hccl
        ${HCCL_HCOMM_HEADER_DIR}
        ${HCCL_HCOMM_HEADER_DIR}
        ${HCCL_BASE_DIR}/pub_inc
        ${HCCL_BASE_DIR}/pub_inc/aicpu
        ${HCCL_BASE_DIR}/pub_inc/inner
        ${HCCL_BASE_DIR}/pub_inc/new
        ${HCCL_BASE_DIR}/platform/hccp/inc
        ${HCCL_BASE_DIR}/platform/hccp/inc/network
        # 临时依赖头文件，待删除
        ${HCCL_HCOMM_HEADER_DIR}/externel_depends/tsch/
        ${THIRD_PARTY_NLOHMANN_PATH}
    )
endif()

if(TARGET json)
    target_link_libraries(asc_ccl_kernel_plf json)
    target_link_libraries(asc_ccl_kernel_plf_a json)
    add_dependencies(asc_ccl_kernel_plf json)
    add_dependencies(asc_ccl_kernel_plf_a json)
endif()

if(BUILD_OPEN_PROJECT)
endif()

target_compile_definitions(asc_ccl_kernel_plf PRIVATE
    $<$<STREQUAL:${PRODUCT_SIDE},host>:_GLIBCXX_USE_CXX11_ABI=0>
)

target_compile_definitions(asc_ccl_kernel_plf_a PRIVATE
    $<$<STREQUAL:${PRODUCT_SIDE},host>:_GLIBCXX_USE_CXX11_ABI=0>
)

# 设置ccl_kernel_plf 包含文件列表
set(ccl_kernel_plf_include_list
    ${include_list}
    ${HCCL_BASE_DIR}/hccl/hccl_comm/wrapper/notify/
    ${HCCL_BASE_DIR}/common/debug/profiling/inc/aicpu
    ${HCCL_BASE_DIR}/framework/common/src/aicpu
)

# 指定ccl_kernel_plf所需头文件搜索路径
target_include_directories(asc_ccl_kernel_plf PRIVATE
    ${ccl_kernel_plf_include_list}
)
# 指定ccl_kernel_plf_a所需头文件搜索路径
target_include_directories(asc_ccl_kernel_plf_a PRIVATE
    ${ccl_kernel_plf_include_list}
)

# 给ccl_kernel_plf添加编译选项
target_compile_options(asc_ccl_kernel_plf PRIVATE
    -Wall
    -fno-common
    -fno-strict-aliasing
    -pipe
    -O3
    -std=c++14
    -D_FORTIFY_SOURCE=2 -O2
    -fstack-protector-all
)

# 给ccl_kernel_plf_a添加编译选项
target_compile_options(asc_ccl_kernel_plf_a PRIVATE
    -Wall
    -fno-common
    -fno-strict-aliasing
    -pipe
    -O3
    -std=c++14
    -ftrapv
    -D_FORTIFY_SOURCE=2 -O2
    -fPIC
    -fstack-protector-all
)
# 为ccl_kernel_plf添加编译定义
target_compile_definitions(asc_ccl_kernel_plf PRIVATE
    HCCD
    CCL_KERNEL
    CCL_KERNEL_AICPU
    USE_AICORE_REDUCESUM
    USE_AICORE_GATHERV2
    USE_AICORE_GATHERV2_INFER
)
# 为ccl_kernel_plf_a添加编译定义
target_compile_definitions(asc_ccl_kernel_plf_a PRIVATE
    HCCD
    CCL_KERNEL
    CCL_KERNEL_AICPU
    USE_AICORE_REDUCESUM
    USE_AICORE_GATHERV2
    USE_AICORE_GATHERV2_INFER
)
# 向ccl_kernel_plf添加链接器选项
target_link_options(asc_ccl_kernel_plf PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -Wl,-Bsymbolic
    -Wl,--exclude-libs,ALL
    -s
)
# 向ccl_kernel_plf_a添加链接器选项
target_link_options(asc_ccl_kernel_plf_a PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -Wl,-Bsymbolic
    -Wl,--exclude-libs,ALL
)

# 将库文件链接到目标ccl_kernel_plf  asc_ccl_kernel_plf_a
if(BUILD_OPEN_PROJECT)
    target_link_libraries(asc_ccl_kernel_plf
        rdma_core_headers
        -Wl,--no-as-needed
        ${HCCL_C_SEC_LINK}
        aicpu_sharder
        ${HCCL_MMPA_LINK}
        -Wl,--as-needed
        -lrt
        -ldl
        -lpthread
        ascend_hal
        c_sec
        unified_dlog
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:atrace_headers>
        $<BUILD_INTERFACE:mmpa_headers>
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:ascend_hal_headers>
    )

    target_link_libraries(asc_ccl_kernel_plf_a
        rdma_core_headers
        -Wl,--no-as-needed
        ${HCCL_C_SEC_LINK}
        aicpu_sharder
        ${HCCL_MMPA_LINK}
        -Wl,--as-needed
        -lrt
        -ldl
        -lpthread
        ascend_hal
        c_sec
        unified_dlog
        $<BUILD_INTERFACE:runtime_headers>
        $<BUILD_INTERFACE:atrace_headers>
        $<BUILD_INTERFACE:mmpa_headers>
        $<BUILD_INTERFACE:acl_rt_headers>
        $<BUILD_INTERFACE:ascend_hal_headers>
    )
endif()

add_subdirectory(framework)
add_subdirectory(platform)

# 指定ccl_kernel_plf构建完成后安装到指定的目标位置
install(TARGETS  asc_ccl_kernel_plf
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)
# 设置ccl_kernel_plf_a输出文件的名字
set_target_properties(asc_ccl_kernel_plf_a
    PROPERTIES
    OUTPUT_NAME asc_ccl_kernel_plf
)
# 指定ccl_kernel_plf_a构建完成后安装到指定的目标位置
install(TARGETS  asc_ccl_kernel_plf_a
    ARCHIVE DESTINATION ${INSTALL_LIBRARY_DIR}  OPTIONAL
)
