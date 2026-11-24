# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16.0)

project(hccl_examples_custom_ops_alltoall_aicpu LANGUAGES CXX)

set(HCCL_CUSTOM_OPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(HCCL_CUSTOM_OPS_INCLUDE_LIST
    ${HCOMM_POD_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host
    ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_aicpu

    # CANN Toolkit头文件
    ${ASCEND_CANN_PACKAGE_PATH}/include
    ${ASCEND_CANN_PACKAGE_PATH}/include/hccl
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/aicpu_ts
    ${ASCEND_CANN_PACKAGE_PATH}/asc/include/comm_api/cpu_ts
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc
    ${ASCEND_CANN_PACKAGE_PATH}/pkg_inc/base  # log_weak.h 内部以 "dlog_pub.h" 引用
)

# Host 动态链接库
add_library(hccl_custom_alltoall_aicpu SHARED)
# 明确指定host库使用CXX编译
set_target_properties(hccl_custom_alltoall_aicpu PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(hccl_custom_alltoall_aicpu PRIVATE
    ${HCCL_CUSTOM_OPS_INCLUDE_LIST}
)
target_compile_options(hccl_custom_alltoall_aicpu PRIVATE
    -U_FORTIFY_SOURCE
    -Werror
    -fno-common
    -fno-strict-aliasing
    -pipe
    -g -O0
    -std=c++17
    -fstack-protector-all
)
target_compile_definitions(hccl_custom_alltoall_aicpu PRIVATE
    _GLIBCXX_USE_CXX11_ABI=0
    -DHOST_COMPILE
    ${ASCCOMM_SOURCE_COMPILE_DEFINITION}
)
target_link_options(hccl_custom_alltoall_aicpu PRIVATE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)
target_link_directories(hccl_custom_alltoall_aicpu PRIVATE
    ${ASCEND_CANN_PACKAGE_PATH}/lib64
    ${ASCEND_CANN_PACKAGE_PATH}/lib64/device/lib64 # 包含 AICPU 动态链接库
)
target_link_libraries(hccl_custom_alltoall_aicpu PRIVATE
    hcomm
    ${ASCCOMM_CPU_LIBRARY}
    c_sec
    unified_dlog

    profapi
    ascendalog
    ascendcl
    runtime
    error_manager
    ascend_dump
)
install(TARGETS hccl_custom_alltoall_aicpu
    LIBRARY DESTINATION ${CUSTOM_OPS_OPP_LIB_PATH}
    COMPONENT hccl
)
install(FILES
    inc/aicpu_ts_custom_alltoall.h
    DESTINATION ${CUSTOM_OPS_OPP_INC_PATH}
    COMPONENT hccl
)

if(KERNEL_MODE)
    # AICPU Kernel 动态链接库
    add_library(custom_alltoall_aicpu_kernel SHARED)
    target_include_directories(custom_alltoall_aicpu_kernel PRIVATE
        ${HCCL_CUSTOM_OPS_INCLUDE_LIST}
    )
    target_compile_definitions(custom_alltoall_aicpu_kernel PRIVATE
        ${ASCCOMM_SOURCE_COMPILE_DEFINITION}
    )
    target_compile_options(custom_alltoall_aicpu_kernel PRIVATE
        -g
        -O0
    )
    # Runtime/HCOMM provides the device-side dependencies; do not embed build-host paths.
    set_target_properties(custom_alltoall_aicpu_kernel PROPERTIES SKIP_BUILD_RPATH TRUE)
    target_link_directories(custom_alltoall_aicpu_kernel PRIVATE
        ${ASCEND_CANN_PACKAGE_PATH}/lib64/device/lib64
        ${ASCEND_CANN_PACKAGE_PATH}/devlib/device
    )
    target_link_libraries(custom_alltoall_aicpu_kernel PRIVATE
        ${ASCCOMM_DATA_PLANE_LIBRARY}
        ${HCOMM_POD_DEVICE_LIBRARY}
    )

    # 打包：AICPU Kernel tar 包
    cann_pack_targets_and_files(
        OUTPUT_TARGET "package_aicpu_hccl_custom_alltoall"
        OUTPUT "aicpu_custom_alltoall.tar.gz"
        TARGETS custom_alltoall_aicpu_kernel
        MANIFEST "bin_hash.cfg"
        TAR_ROOT_DIR "aicpu_kernels_device"
    )

    # 对 AICPU Kernel tar 包签名，设备侧加载流程要求使用签名包。
    add_cann_sign_file(
        INPUT "aicpu_custom_alltoall.tar.gz"
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scripts/custom_alltoall_aicpu_check_cfg.xml
        VERSION "9.2.0"
        RESULT_VAR "sign_aicpu_custom_alltoall_result"
        DEPENDS package_aicpu_hccl_custom_alltoall
    )

    # 注册签名目标，确保设备构建阶段产出签名包。
    register_custom_aicpu_target(sign_aicpu_custom_alltoall)
else()
    # 安装
    set(_aicpu_package "${BUILD_DEVICE_DIR}/alltoall/aicpu_custom_alltoall.tar.gz")
    install(FILES
        ${_aicpu_package}
        DESTINATION ${CUSTOM_OPS_OPP_AICPU_KERNEL_PATH}
        COMPONENT hccl
    )
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_aicpu/libcustom_alltoall_aicpu_kernel.json
        DESTINATION ${CUSTOM_OPS_OPP_AICPU_CONFIG_PATH}
        COMPONENT hccl
    )
endif()

# Host source files
target_sources(hccl_custom_alltoall_aicpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/op_host/alltoall.cc
)

# AICPU kernel source files
if(KERNEL_MODE)
    target_sources(custom_alltoall_aicpu_kernel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/op_kernel_aicpu/aicpu_kernel.cc
    )
endif()
